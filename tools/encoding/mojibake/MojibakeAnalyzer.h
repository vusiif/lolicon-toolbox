#pragma once
#include "MojibakeCandidate.h"
#include <functional>
namespace mojibake {
class MojibakeAnalyzer {
public:
    static constexpr qsizetype MaxInputBytes = 1024 * 1024;
    // progress (0..1) is reported from worker threads during lossy reconstruction only.
    static Result analyze(const QString& input, const Cancellation& cancel = {},
                          const std::function<void(double)>& progress = {});
    static bool lossyQuestionMarks(const QString& text);
    // Quality index of a lossy reconstruction: unknown and model-chosen characters count as damage.
    static double lossyScore(const QString& text, int partial, int unknown);
};
}
