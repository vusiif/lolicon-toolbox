#pragma once
#include "MojibakeCandidate.h"
namespace mojibake {
class MojibakeAnalyzer {
public:
    static constexpr qsizetype MaxInputBytes = 1024 * 1024;
    static Result analyze(const QString& input, const Cancellation& cancel = {});
};
}
