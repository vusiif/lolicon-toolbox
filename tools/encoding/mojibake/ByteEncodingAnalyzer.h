#pragma once
#include "MojibakeCandidate.h"
namespace mojibake {
struct FileCandidate {
    QString text;
    Encoding encoding = Encoding::Utf8;
    QList<Encoding> aliases;
    QList<Transformation> repairPath;
    double score = 0;
    bool reversible = true;
};
struct FileResult {
    QString path;
    QByteArray bytes, hash;
    QList<FileCandidate> candidates;
    std::optional<Encoding> bomEncoding;
    std::optional<Encoding> preferredEncoding;
    bool cancelled = false, tooLarge = false, probablyBinary = false, invalidBom = false;
    bool inverseRepairSkipped = false;
    QString error;
};
class ByteEncodingAnalyzer {
public:
    static constexpr qsizetype MaxFileBytes = 10 * 1024 * 1024;
    static FileResult load(const QString& path, std::optional<Encoding> forced = {}, const Cancellation& cancel = {});
    static FileResult analyze(const QByteArray& bytes, std::optional<Encoding> forced = {}, const Cancellation& cancel = {});
    static QByteArray bom(Encoding encoding);
};
}
