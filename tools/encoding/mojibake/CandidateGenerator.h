#pragma once
#include "MojibakeCandidate.h"
namespace mojibake {
class CandidateGenerator {
public:
    static QList<Candidate> generate(const QString& text, const Cancellation& cancel, bool& limited);
    // Exposed for path verification and round-trip tests.
    static std::optional<QString> transform(const QString& text, const Transformation& step);
};
}
