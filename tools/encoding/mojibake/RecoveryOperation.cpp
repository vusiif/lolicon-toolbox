#include "RecoveryOperation.h"
#include <QFile>
#include <QCryptographicHash>
#include <QCoreApplication>
namespace mojibake {
namespace { QString tr(const char* text) { return QCoreApplication::translate("RecoveryOperation", text); } }
std::optional<QByteArray> RecoveryOperation::output(const QString& original, const Encoding encoding, const bool writeBom, const Newlines newlines) {
    auto text = original;
    if (newlines != Newlines::Preserve) {
        text.replace("\r\n", "\n").replace('\r', '\n');
        if (newlines == Newlines::CRLF) text.replace("\n", "\r\n");
    }
    auto bytes = EncodingConverter::encode(text, encoding);
    if (!bytes || (writeBom && ByteEncodingAnalyzer::bom(encoding).isEmpty())) return {};
    if (writeBom) bytes->prepend(ByteEncodingAnalyzer::bom(encoding));
    return bytes;
}
SaveResult RecoveryOperation::saveCopy(const FileResult& snapshot, const int candidate, const QString& destination,
                                      const Encoding encoding, const bool writeBom, const Newlines newlines) {
    if (candidate < 0 || candidate >= snapshot.candidates.size() || snapshot.path.isEmpty()) return {.success = false, .error = tr("No file candidate selected.")};
    const auto bytes = output(snapshot.candidates[candidate].text, encoding, writeBom, newlines);
    if (!bytes) return {.success = false, .error = tr("The selected output encoding cannot represent all characters without loss.")};
    QFile source(snapshot.path);
    if (!source.open(QIODevice::ReadOnly)) return {.success = false, .error = tr("The source file is no longer readable. Analyze it again.")};
    if (source.size() > ByteEncodingAnalyzer::MaxFileBytes ||
        QCryptographicHash::hash(source.read(ByteEncodingAnalyzer::MaxFileBytes + 1), QCryptographicHash::Sha256) != snapshot.hash)
        return {.success = false, .error = tr("The source file changed after analysis. Analyze it again before exporting.")};
    source.close();
    // Exclusive creation prevents overwrite even if another process races the
    // save dialog, or the destination aliases the original through a link.
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return {.success = false, .error = tr("Cannot create the copy. Choose a new filename; existing files are never overwritten.")};
    qsizetype written = 0;
    while (written < bytes->size()) {
        const auto count = file.write(bytes->constData() + written, bytes->size() - written);
        if (count <= 0) { file.close(); file.remove(); return {.success = false, .error = tr("Could not write the complete copy.")}; }
        written += count;
    }
    if (!file.flush()) { file.close(); file.remove(); return {.success = false, .error = tr("Could not write the complete copy.")}; }
    file.close(); return {.success = true, .error = {}};
}
}
