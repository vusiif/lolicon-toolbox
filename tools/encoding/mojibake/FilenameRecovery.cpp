#include "FilenameRecovery.h"
#include "MojibakeAnalyzer.h"
#include "CandidateScorer.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QHash>
#include <algorithm>
namespace mojibake {
namespace {
QString tr(const char* text) { return QCoreApplication::translate("FilenameRecovery", text); }
bool cancelled(const Cancellation& token) { return token && token->load(); }
bool link(const QFileInfo& info) { return info.isSymLink() || info.isJunction(); }
bool containsPath(const QString& parent, const QString& child) {
    const auto a = QDir::cleanPath(parent), b = QDir::cleanPath(child);
    return a.compare(b, Qt::CaseInsensitive) == 0 || b.startsWith(a.endsWith('/') ? a : a + '/', Qt::CaseInsensitive);
}
QList<Candidate> candidates(const QString& name, bool directory, const Cancellation& cancel) {
    const int dot = directory ? -1 : name.lastIndexOf('.');
    const auto stem = dot > 0 ? name.left(dot) : name;
    const auto suffix = dot > 0 ? name.mid(dot) : QString();
    if (CandidateScorer::anomalies(stem) == 0)
        return {{name, {}, CandidateScorer::score(Candidate{name, {}}), 0, true}};
    auto result = MojibakeAnalyzer::analyze(stem, cancel);
    for (auto& c : result.candidates) c.text += suffix;
    // Keep the original name available even when it was outside the top five.
    if (std::none_of(result.candidates.begin(), result.candidates.end(), [&](const auto& c) { return c.text == name; })) {
        if (result.candidates.size() == 5) result.candidates.removeLast();
        result.candidates.append({name, {}, CandidateScorer::score(Candidate{name, {}}), CandidateScorer::anomalies(stem), true});
    }
    return result.candidates;
}
NamePlan scanTree(const QString& root, bool analyzeNames, const Cancellation& cancel, const NameProgressPtr& progress) {
    NamePlan plan;
    const QFileInfo rootInfo(root);
    if (link(rootInfo) || !rootInfo.isDir()) { plan.error = tr("Choose a regular directory, not a link or junction."); return plan; }
    plan.root = rootInfo.canonicalFilePath();
    if (plan.root.isEmpty()) { plan.error = tr("The source directory is not accessible."); return plan; }
    struct Pending { QString path; int parent, depth; };
    QList<Pending> pending{{plan.root, -1, 0}};
    while (!pending.isEmpty()) {
        const auto current = pending.takeLast();
        // QDirIterator does not follow links. Check the directory again before listing.
        const QFileInfo directory(current.path);
        if (link(directory) || !directory.isDir() || !directory.isReadable()) {
            plan.error = tr("A source directory changed or cannot be read."); break;
        }
        QDirIterator iterator(current.path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (iterator.hasNext()) {
            if (cancelled(cancel)) { plan.cancelled = true; return plan; }
            iterator.next(); const auto info = iterator.fileInfo();
            if (plan.entries.size() >= FilenameRecovery::MaxEntries || current.depth >= FilenameRecovery::MaxDepth) {
                plan.error = tr("Scan limit reached: 1,000 entries, 32 levels or 256 MiB. Choose a smaller directory."); return plan;
            }
            if (link(info) || (!info.isDir() && !info.isFile())) {
                plan.error = tr("Links, junctions and special files are not supported. Choose a directory without them."); return plan;
            }
            NameEntry entry;
            entry.parent = current.parent; entry.directory = info.isDir();
            entry.relativePath = QDir(plan.root).relativeFilePath(info.absoluteFilePath());
            entry.selectedName = info.fileName();
            if (!entry.directory) {
                QFile file(info.absoluteFilePath());
                if (!file.open(QIODevice::ReadOnly)) { plan.error = tr("A source file cannot be read."); return plan; }
                if (file.size() > FilenameRecovery::MaxTotalBytes - plan.totalBytes) {
                    plan.error = tr("Scan limit reached: 1,000 entries, 32 levels or 256 MiB. Choose a smaller directory."); return plan;
                }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                while (!file.atEnd()) {
                    if (cancelled(cancel)) { plan.cancelled = true; return plan; }
                    const auto chunk = file.read(1024 * 1024);
                    if (file.error() != QFileDevice::NoError) { plan.error = tr("A source file cannot be read."); return plan; }
                    entry.size += chunk.size();
                    if (entry.size > FilenameRecovery::MaxTotalBytes - plan.totalBytes) {
                        plan.error = tr("Scan limit reached: 1,000 entries, 32 levels or 256 MiB. Choose a smaller directory."); return plan;
                    }
                    hash.addData(chunk);
                }
                entry.hash = hash.result(); plan.totalBytes += entry.size;
            }
            if (analyzeNames) entry.candidates = candidates(info.fileName(), entry.directory, cancel);
            const int index = plan.entries.size(); plan.entries.append(std::move(entry));
            if (info.isDir()) pending.append({info.absoluteFilePath(), index, current.depth + 1});
            if (progress) { progress->entries.store(plan.entries.size()); progress->bytes.store(plan.totalBytes); }
        }
    }
    if (cancelled(cancel)) plan.cancelled = true;
    return plan;
}
bool sameSource(const NamePlan& a, const NamePlan& b) {
    if (!b.error.isEmpty() || b.cancelled || a.root != b.root || a.entries.size() != b.entries.size()) return false;
    QHash<QString, const NameEntry*> entries;
    for (const auto& e : a.entries) entries.insert(e.relativePath, &e);
    for (const auto& e : b.entries) {
        const auto it = entries.constFind(e.relativePath);
        if (it == entries.cend() || (*it)->directory != e.directory || (*it)->size != e.size || (*it)->hash != e.hash) return false;
    }
    return true;
}
}
bool NameValidation::valid() const { return std::all_of(errors.begin(), errors.end(), [](const auto& error) { return error.isEmpty(); }); }
NamePlan FilenameRecovery::scan(const QString& root, const Cancellation& cancel, const NameProgressPtr& progress) {
    return scanTree(root, true, cancel, progress);
}
QString FilenameRecovery::invalidName(const QString& name) {
    if (name.isEmpty() || name == "." || name == ".." || name.size() > 255 || !name.isValidUtf16())
        return tr("Empty, invalid or overly long name.");
    if (name.endsWith(' ') || name.endsWith('.')) return tr("Windows names cannot end in a space or dot.");
    for (auto c : name) if (c.unicode() < 32 || QString("<>:\"/\\|?*").contains(c))
        return tr("The name contains a Windows-forbidden character.");
    const auto base = name.section('.', 0, 0).trimmed().toUpper();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" || base == "CONOUT$" ||
        ((base.startsWith("COM") || base.startsWith("LPT")) && base.size() == 4 &&
         (QString("123456789¹²³").contains(base[3])))) return tr("Reserved Windows device name.");
    return {};
}
NameValidation FilenameRecovery::validate(const NamePlan& plan) {
    NameValidation result; QHash<QString, int> names;
    result.errors = QStringList(plan.entries.size(), QString());
    for (int i = 0; i < plan.entries.size(); ++i) {
        const auto& e = plan.entries[i];
        auto error = invalidName(e.selectedName);
        if (e.parent < -1 || e.parent >= i || (e.parent >= 0 && !plan.entries[e.parent].directory)) error = tr("Invalid directory hierarchy.");
        const auto parent = e.parent >= 0 && e.parent < i ? result.paths[e.parent] : QString();
        const auto path = parent.isEmpty() ? e.selectedName : parent + '/' + e.selectedName;
        result.paths.append(path); result.errors[i] = error;
        const auto key = QString::number(e.parent) + ':' + e.selectedName.toCaseFolded();
        if (names.contains(key)) {
            result.errors[i] = tr("Name conflict in the same directory (case-insensitive).");
            result.errors[names[key]] = result.errors[i];
        } else names.insert(key, i);
    }
    return result;
}
NameCopyResult FilenameRecovery::copy(const NamePlan& plan, const QString& destination, const Cancellation& cancel, const NameProgressPtr& progress) {
    NameCopyResult result;
    auto fail = [&](const QString& error) { result.error = error; result.cancelled = cancelled(cancel); return result; };
    if (!plan.error.isEmpty() || plan.cancelled || plan.root.isEmpty() || !validate(plan).valid()) return fail(tr("The plan contains errors. Fix them or scan again."));
    const QFileInfo target(QDir::cleanPath(QFileInfo(destination).absoluteFilePath()));
    const auto parent = QFileInfo(target.absolutePath()).canonicalFilePath();
    if (parent.isEmpty() || !invalidName(target.fileName()).isEmpty()) return fail(tr("Choose a new output directory inside an existing parent directory."));
    result.destination = QDir(parent).filePath(target.fileName());
    if (containsPath(plan.root, result.destination) || containsPath(result.destination, plan.root))
        return fail(tr("The output must be outside the source directory and cannot contain it."));
    if (target.exists() || link(target)) return fail(tr("The output already exists. Choose a new directory; nothing is overwritten."));
    const auto before = scanTree(plan.root, false, cancel, {});
    if (cancelled(cancel)) return fail(tr("Copy cancelled."));
    if (!sameSource(plan, before)) return fail(tr("The source tree changed or is unreadable. Scan again before copying."));
    const auto validation = validate(plan);
    for (const auto& path : validation.paths)
        if (result.destination.size() + path.size() + 1 >= 32760) return fail(tr("An output path is too long. Choose a shorter destination or name."));
    if (!QDir(parent).mkdir(target.fileName())) return fail(tr("Cannot create the output directory. Nothing is overwritten."));
    result.outputCreated = true;
    // On failure retain the partial output, clearly marked in the UI. Never
    // recursively remove a user-selected tree or silently retry into it.
    for (int i = 0; i < plan.entries.size(); ++i) {
        if (cancelled(cancel)) return fail(tr("Copy cancelled."));
        const auto& e = plan.entries[i];
        const auto sourcePath = QDir(plan.root).filePath(e.relativePath);
        const QFileInfo source(sourcePath);
        const auto outputPath = QDir(result.destination).filePath(validation.paths[i]);
        if (link(QFileInfo(result.destination)) || !QFileInfo(result.destination).isDir())
            return fail(tr("The output directory changed during copying. Stop and choose a new destination."));
        for (int index = e.parent; index >= 0; index = plan.entries[index].parent) {
            const QFileInfo ancestor(QDir(result.destination).filePath(validation.paths[index]));
            if (link(ancestor) || !ancestor.isDir())
                return fail(tr("The output directory changed during copying. Stop and choose a new destination."));
        }
        // Check ancestors too: a directory may have been replaced by a link.
        for (int index = e.parent; index >= 0; index = plan.entries[index].parent) {
            const QFileInfo ancestor(QDir(plan.root).filePath(plan.entries[index].relativePath));
            if (link(ancestor) || !ancestor.isDir()) return fail(tr("The source tree changed or is unreadable. Scan again before copying."));
        }
        if (link(QFileInfo(plan.root)) || link(source) || e.directory != source.isDir())
            return fail(tr("The source tree changed or is unreadable. Scan again before copying."));
        if (e.directory) {
            const QFileInfo out(outputPath);
            if (!QDir(out.absolutePath()).mkdir(out.fileName())) return fail(tr("Could not create a copied directory."));
        } else {
            QFile input(sourcePath), output(outputPath);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return fail(tr("Could not read a source file or exclusively create its copy."));
            QCryptographicHash hash(QCryptographicHash::Sha256); qint64 bytes = 0;
            while (!input.atEnd()) {
                if (cancelled(cancel)) return fail(tr("Copy cancelled."));
                const auto chunk = input.read(1024 * 1024);
                if (input.error() != QFileDevice::NoError || bytes + chunk.size() > e.size)
                    return fail(tr("The source tree changed or is unreadable. Scan again before copying."));
                qsizetype written = 0;
                while (written < chunk.size()) {
                    const auto n = output.write(chunk.constData() + written, chunk.size() - written);
                    if (n <= 0) return fail(tr("Could not write the complete directory copy."));
                    written += n;
                }
                hash.addData(chunk); bytes += chunk.size();
                if (progress) progress->bytes.fetch_add(chunk.size());
            }
            if (bytes != e.size || hash.result() != e.hash) return fail(tr("The source tree changed or is unreadable. Scan again before copying."));
            if (!output.flush()) return fail(tr("Could not write the complete directory copy."));
        }
        if (progress) progress->entries.store(i + 1);
    }
    const auto after = scanTree(plan.root, false, cancel, {});
    if (cancelled(cancel)) return fail(tr("Copy cancelled."));
    if (!sameSource(plan, after)) return fail(tr("The source tree changed during copying. The output is incomplete; scan again."));
    result.success = true; return result;
}
}
