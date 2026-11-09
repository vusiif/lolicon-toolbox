#include "MojibakeAnalyzer.h"
#include "CandidateGenerator.h"
#include "CandidateScorer.h"
#include <algorithm>
namespace mojibake {
// Lossy conversions write one '?' per unrepresentable character. A run of two or
// more counts as possible loss unless it reads as emphatic punctuation: it follows
// a letter or CJK character and ends the text or is followed by whitespace.
bool MojibakeAnalyzer::lossyQuestionMarks(const QString& text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != '?') continue;
        qsizetype end = i;
        while (end < text.size() && text[end] == '?') ++end;
        if (end - i >= 2) {
            const bool afterWord = i > 0 && text[i - 1].isLetterOrNumber();
            const bool closes = end == text.size() || text[end].isSpace();
            if (!(afterWord && closes)) return true;
        }
        i = end - 1;
    }
    return false;
}
Result MojibakeAnalyzer::analyze(const QString& input, const Cancellation& cancel) {
    Result result;
    result.original = input;
    result.possibleLoss = input.contains(QChar(0xfffd)) || lossyQuestionMarks(input);
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
