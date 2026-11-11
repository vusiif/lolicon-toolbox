#pragma once
#include "ByteEncodingAnalyzer.h"
namespace mojibake {
enum class Newlines { Preserve, LF, CRLF };
struct SaveResult { bool success = false; QString error; };
class RecoveryOperation {
public:
    static std::optional<QByteArray> output(const QString& original, Encoding encoding, bool writeBom, Newlines newlines);
    static SaveResult saveCopy(const FileResult& snapshot, int candidate, const QString& destination,
                               Encoding encoding, bool writeBom, Newlines newlines);
};
}
