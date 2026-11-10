#include "MojibakeAnalyzer.h"
#include "CandidateGenerator.h"
#include "CandidateScorer.h"
#include "LossyGbkRecovery.h"
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
double MojibakeAnalyzer::lossyScore(const QString& text, int partial, int unknown) {
    const double bad = unknown + 0.35 * partial + CandidateScorer::rareCharacters(text);
    const double length = std::max<qsizetype>(8, text.size());
    return std::clamp(100.0 / (1.0 + 4.0 * bad / length) - 16.5, 0.0, 100.0);
}
Result MojibakeAnalyzer::analyze(const QString& input, const Cancellation& cancel, const std::function<void(double)>& progress) {
    Result result;
    result.original = input;
    result.possibleLoss = input.contains(QChar(0xfffd)) || lossyQuestionMarks(input);
    if (input.size() > MaxInputBytes || input.toUtf8().size() > MaxInputBytes) { result.tooLarge = true; return result; }
    if (input.isEmpty()) return result;
    // GBK bytes that were read as UTF-8 lost every byte that was not valid UTF-8.
    // Re-encoding cannot reach that text; rebuild the bytes instead.
    std::optional<Candidate> lossy;
    if (LossyGbkRecovery::applicable(input)) {
        const auto recovery = LossyGbkRecovery::recover(input, cancel, progress);
        if (recovery.cancelled || (cancel && cancel->load())) { result.cancelled = true; result.candidates.clear(); return result; }
        if (recovery.success && recovery.exact + recovery.partial > 0) {
            Candidate c{recovery.text, {Transformation{Encoding::Utf8, Encoding::Gbk, false, false, true}}};
            c.reversible = false;
            c.evidence = recovery.evidence;
            c.lossy = std::make_shared<const LossyRecovery>(recovery);
            c.anomalies = CandidateScorer::anomalies(c.text);
            c.score = lossyScore(c.text, recovery.partial, recovery.unknown);
            lossy = c;
        }
    }
    // A reconstruction is the only candidate that explains the replacement pattern.
    // Whole-string conversions of such text merely re-encode U+FFFD (锟斤拷, 占쏙옙,
    // ï¿½...) and can look clean to the scorer, so they are not searched at all.
    if (lossy) {
        Candidate original{input, {}};
        original.score = CandidateScorer::score(original);
        original.anomalies = CandidateScorer::anomalies(input);
        result.candidates = {*lossy, original};
        result.generated = 2;
    } else {
        result.candidates = CandidateGenerator::generate(input, cancel, result.searchLimited);
        result.generated = result.candidates.size();
        if (cancel && cancel->load()) { result.cancelled = true; result.candidates.clear(); return result; }
        std::stable_sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    }
    const double originalScore = CandidateScorer::score(Candidate{input, {}});
    const double gain = result.candidates.first().score - originalScore;
    const bool partial = std::any_of(result.candidates.first().path.begin(), result.candidates.first().path.end(), [](const auto& step) { return step.partial || step.lossy; });
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
