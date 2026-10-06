#include "MojibakeAnalyzer.h"
#include "CandidateGenerator.h"
#include "CandidateScorer.h"
#include <algorithm>
namespace mojibake {
Result MojibakeAnalyzer::analyze(const QString& input, const Cancellation& cancel) {
    Result result;
    result.original = input;
    result.possibleLoss = input.contains(QChar(0xfffd)) || input.contains("??");
    if (input.size() > MaxInputBytes || input.toUtf8().size() > MaxInputBytes) { result.tooLarge = true; return result; }
    if (input.isEmpty()) return result;
    result.candidates = CandidateGenerator::generate(input, cancel, result.searchLimited);
    result.generated = result.candidates.size();
    if (cancel && cancel->load()) { result.cancelled = true; result.candidates.clear(); return result; }
    std::stable_sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    const double originalScore = CandidateScorer::score(Candidate{input, {}});
    const double gain = result.candidates.first().score - originalScore;
    const bool partial = std::any_of(result.candidates.first().path.begin(), result.candidates.first().path.end(), [](const auto& step) { return step.partial; });
    result.possibleLoss = result.possibleLoss || partial;
    result.likelyMojibake = result.candidates.first().text != input && (gain >= 8 || partial) &&
                          result.candidates.first().anomalies < CandidateScorer::anomalies(input);
    if (!result.likelyMojibake) {
        auto it = std::find_if(result.candidates.begin(), result.candidates.end(), [&](const auto& c) { return c.text == input; });
        if (it != result.candidates.end()) std::rotate(result.candidates.begin(), it, it + 1);
    } else if (!result.possibleLoss) {
        const double gap = result.candidates.size() > 1 ? result.candidates[0].score - result.candidates[1].score : 100;
        result.confidence = gap >= 8 && gain >= 20 && result.candidates[0].reversible ? Confidence::High : Confidence::Medium;
    }
    result.candidates = result.candidates.mid(0, 5);
    return result;
}
}
