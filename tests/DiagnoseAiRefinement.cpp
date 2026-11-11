// Manual tool, not a ctest test: it calls the AI service configured in the app
// and costs real requests (see RunAiRefinement.cpp for the key handling).
//
// The corpus damage (one U+FFFD per maximal subpart) hides how many characters
// were lost, so AI answers cannot be compared with the original position by
// position. Here each original is damaged the per-byte way (Qt, .NET) instead:
// the character count survives, every position lines up, and the tool counts
// exactly where the AI's per-span fills are right, wrong, or off the byte.
//
// DiagnoseAiRefinement <corpus dir> <report file> [--model <id>]
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <deque>
#include "MojibakeAi.h"
#include "ai/AiService.h"
#include "services/AppSettings.h"
using namespace mojibake;
namespace {
struct Counts {
    int partial = 0, partialLocal = 0, partialFill = 0, partialFillFits = 0, partialFinal = 0, partialAnswered = 0;
    int lost = 0, lostFill = 0, lostFillBox = 0, lostFinal = 0, lostFinalBox = 0, lostAnswered = 0;
    int exactChanged = 0, units = 0, answered = 0, wrongLength = 0, chunks = 0, failed = 0;
    void add(const Counts& o) {
        partial += o.partial; partialLocal += o.partialLocal; partialFill += o.partialFill; partialFillFits += o.partialFillFits;
        partialFinal += o.partialFinal; partialAnswered += o.partialAnswered; lost += o.lost; lostFill += o.lostFill;
        lostFillBox += o.lostFillBox; lostFinal += o.lostFinal; lostFinalBox += o.lostFinalBox; lostAnswered += o.lostAnswered;
        exactChanged += o.exactChanged; units += o.units; answered += o.answered; wrongLength += o.wrongLength;
        chunks += o.chunks; failed += o.failed;
    }
};
QString rate(const int part, const int whole) { return QStringLiteral("%1/%2 (%3%)").arg(part).arg(whole).arg(whole ? 100.0 * part / whole : 0.0, 0, 'f', 1); }
}
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3) { qCritical("usage: DiagnoseAiRefinement <corpus dir> <report file> [--model <id>]"); return 2; }
    AppSettings settings{QString()};
    QTemporaryDir scratch;
    std::unique_ptr<AppSettings> copy;
    AppSettings* active = &settings;
    if (const auto at = args.indexOf("--model"); at > 0 && at + 1 < args.size()) {
        auto configuration = settings.aiConfiguration();
        configuration.model = args[at + 1];
        copy = std::make_unique<AppSettings>(scratch.filePath("settings.ini"));
        if (!copy->setAiConfiguration(configuration)) { qCritical("Cannot use the configured key."); return 2; }
        active = copy.get();
    }
    QFile report(args[2]);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return 2;
    const auto note = [&report](const QString& text) { report.write(text.toUtf8() + '\n'); report.flush(); };
    note("Model: " + active->aiConfiguration().model + ", per-span fills, per-byte damage");
    const QDir corpus(args[1]);
    Counts total;
    const auto print = [&](const QString& title, const Counts& n) {
        note("## " + title);
        note(QStringLiteral("chunks %1, failed %2; spans %3, answered %4, wrong length %5").arg(n.chunks).arg(n.failed).arg(n.units).arg(n.answered).arg(n.wrongLength));
        note("partial (one byte left): local right " + rate(n.partialLocal, n.partial) + ", after AI right " + rate(n.partialFinal, n.partial));
        note("    raw fills: right " + rate(n.partialFill, n.partialAnswered) + ", fit the surviving byte " + rate(n.partialFillFits, n.partialAnswered));
        note("lost (no byte left): after AI right " + rate(n.lostFinal, n.lost) + ", still □ " + rate(n.lostFinalBox, n.lost));
        note("    raw fills: right " + rate(n.lostFill, n.lostAnswered) + ", left as □ " + rate(n.lostFillBox, n.lostAnswered));
        note(QStringLiteral("exact characters changed: %1").arg(n.exactChanged));
    };
    for (const auto& document : corpus.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile file(corpus.filePath(document + "/ground_truth.txt"));
        if (!file.open(QIODevice::ReadOnly)) continue;
        auto truth = QString::fromUtf8(file.readAll());
        if (truth.startsWith(QChar(0xfeff))) truth.remove(0, 1);
        QStringEncoder gbk("GBK", QStringConverter::Flag::Stateless);
        const QByteArray bytes = gbk(truth);
        if (gbk.hasError()) { note(document + ": not GBK encodable, skipped"); continue; }
        const auto damaged = LossyGbkRecovery::lossyUtf8(bytes, Utf8Replacement::PerByte);
        const auto local = LossyGbkRecovery::recover(damaged);
        if (!local.success || local.replacement != Utf8Replacement::PerByte || local.text.size() != truth.size()) {
            note(document + ": local result does not line up with the original, skipped"); continue;
        }
        const auto glossary = LossyGbkRecovery::glossary(local, MojibakeAi::GlossaryCharacters);
        // Chunks as MojibakeAi cuts them: whole spans up to ChunkCharacters.
        std::vector<LossySpan> chunks;
        LossySpan open{}; bool started = false;
        for (const auto& s : local.spans) {
            if (!started) { open = s; started = true; } else { open.damagedEnd = s.damagedEnd; open.textEnd = s.textEnd; }
            if (open.textEnd - open.textBegin >= MojibakeAi::ChunkCharacters) { chunks.push_back(open); started = false; }
        }
        if (started) chunks.push_back(open);
        QString final = local.text;
        Counts counts;
        std::deque<int> queue;
        for (int k = 0; k < static_cast<int>(chunks.size()); ++k) queue.push_back(k);
        std::vector<std::unique_ptr<ai::AiService>> services;
        int running = 0;
        QEventLoop loop;
        std::function<void(ai::AiService*)> next = [&](ai::AiService* service) {
            if (queue.empty()) { if (running == 0) loop.quit(); return; }
            const int k = queue.front(); queue.pop_front();
            const auto c = chunks[k];
            const auto units = LossyGbkRecovery::repairUnits(local, c.textBegin, c.textEnd, MojibakeAi::UnitCharacters);
            if (units.isEmpty()) { next(service); return; }
            ++running; ++counts.chunks; counts.units += static_cast<int>(units.size());
            QObject::disconnect(service, nullptr, &loop, nullptr);
            QObject::connect(service, &ai::AiService::completed, &loop, [&, service, c, units](const ai::Reply& reply) {
                --running;
                QHash<int, QString> fills;
                for (auto it = reply.fills.begin(); it != reply.fills.end(); ++it) {
                    bool ok = false;
                    if (const int id = it.key().toInt(&ok); ok && id >= 1 && id <= units.size()) fills.insert(id, it.value().toString());
                }
                counts.answered += static_cast<int>(fills.size());
                for (qsizetype u = 0; u < units.size(); ++u) {
                    const auto& unit = units[u];
                    const auto fill = fills.value(static_cast<int>(u + 1));
                    const bool aligned = fill.size() == unit.textEnd - unit.textBegin;
                    if (!fill.isEmpty() && !aligned) ++counts.wrongLength;
                    for (auto i = unit.textBegin; i < unit.textEnd; ++i) {
                        const QChar f = aligned ? fill[i - unit.textBegin] : QChar();
                        if (static_cast<CharEvidence>(local.evidence[i]) == CharEvidence::Partial) {
                            if (!aligned) continue;
                            ++counts.partialAnswered;
                            counts.partialFill += f == truth[i];
                            counts.partialFillFits += LossyGbkRecovery::alternatives(local, i, 400).contains(f);
                        } else if (aligned) {
                            ++counts.lostAnswered;
                            counts.lostFill += f == truth[i];
                            counts.lostFillBox += f == QChar(LossyGbkRecovery::UnknownMark);
                        }
                    }
                }
                const auto guide = LossyGbkRecovery::unitGuide(local, c.textBegin, c.textEnd, units, fills);
                const auto result = LossyGbkRecovery::guided(damaged.mid(c.damagedBegin, c.damagedEnd - c.damagedBegin), guide, Utf8Replacement::PerByte);
                if (result.success && result.text.size() == c.textEnd - c.textBegin) final.replace(c.textBegin, result.text.size(), result.text);
                else ++counts.failed;
                next(service);
            });
            QObject::connect(service, &ai::AiService::failed, &loop, [&, service](const QString& error) {
                --running; ++counts.failed; note("    chunk failed: " + error); next(service);
            });
            service->analyze(active->aiConfiguration(),
                             MojibakeAi::unitsRequest(local, c.textBegin, c.textEnd, units, k + 1, static_cast<int>(chunks.size()), glossary),
                             active->locale());
        };
        for (int i = 0; i < MojibakeAi::Parallel; ++i) services.push_back(std::make_unique<ai::AiService>());
        for (auto& s : services) next(s.get());
        if (running) loop.exec();
        for (qsizetype i = 0; i < truth.size(); ++i) {
            switch (static_cast<CharEvidence>(local.evidence[i])) {
            case CharEvidence::Exact: counts.exactChanged += final[i] != truth[i]; break;
            case CharEvidence::Partial: ++counts.partial; counts.partialLocal += local.text[i] == truth[i]; counts.partialFinal += final[i] == truth[i]; break;
            default: ++counts.lost; counts.lostFinal += final[i] == truth[i]; counts.lostFinalBox += final[i] == QChar(LossyGbkRecovery::UnknownMark); break;
            }
        }
        print(document, counts);
        total.add(counts);
    }
    print("TOTAL", total);
    return 0;
}
