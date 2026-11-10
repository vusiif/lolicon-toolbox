#pragma once
#include "MojibakeCandidate.h"
#include <functional>
namespace mojibake {
struct FileCandidate {
    QString text;
    Encoding encoding = Encoding::Utf8;
    QList<Encoding> aliases;
    QList<Transformation> repairPath;
    double score = 0;
    bool reversible = true;
    int anomalies = 0;
    QByteArray evidence;  // See Candidate::evidence.
    std::shared_ptr<const LossyRecovery> lossy;  // See Candidate::lossy.
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
    // progress (0..1, from worker threads) is reported while a lossy GBK reconstruction runs.
    static FileResult load(const QString& path, std::optional<Encoding> forced = {}, const Cancellation& cancel = {},
                           const std::function<void(double)>& progress = {});
    static FileResult analyze(const QByteArray& bytes, std::optional<Encoding> forced = {}, const Cancellation& cancel = {},
                              const std::function<void(double)>& progress = {});
    static QByteArray bom(Encoding encoding);
};
}
