#include "CandidateScorer.h"
#include <QRegularExpression>
#include <algorithm>
namespace mojibake {
int CandidateScorer::anomalies(const QString& text) {
    int count = 0;
    for (auto c : text) {
        const auto u = c.unicode();
        if (u == 0xfffd) count += 8;
        else if (u >= 0xe000 && u <= 0xf8ff) ++count;
        else if ((u < 32 && c != '\n' && c != '\r' && c != '\t') || (u >= 0x7f && u < 0xa0)) count += 3;
    }
    // Multi-character evidence, not a blanket penalty for accented/CJK letters.
    static const QRegularExpression patterns(QString::fromUtf8(
        "[\\x{00C2}-\\x{00F4}][\\x{0080}-\\x{00BF}€™œžšŸ]|浣犲|璇峰|缁勮|鎶ュ|椤圭|銆[傘備]|鈥[檚滙]|縺[薙繧]|锟斤拷|[\\x{00A0}-\\x{00BF}]{2,}"));
    auto matches = patterns.globalMatch(text);
    while (matches.hasNext()) { matches.next(); ++count; }
    return count;
}
double CandidateScorer::score(const Candidate& candidate) {
    double bad = anomalies(candidate.text);
    const bool partial = std::any_of(candidate.path.begin(), candidate.path.end(), [](const auto& step) { return step.partial; });
    // A marked hole in a partial repair is expected missing information, not
    // eight new encoding errors. Keep a separate penalty for incomplete repair.
    if (partial) bad -= 7.0 * candidate.text.count(QChar(0xfffd));
    // Normalize by length: repeating a paragraph must not make its quality
    // arbitrarily negative. This is a quality index, never a probability.
    const double length = std::max<qsizetype>(8, candidate.text.size());
    double value = 100.0 / (1.0 + 4.0 * bad / length);
    value -= 1.5 * candidate.path.size();
    // Equally clean legacy decodings often produce unrelated CJK glyphs.
    // Keep the UTF-8 prior explicit, below the anomaly and damage penalties.
    if (!candidate.path.isEmpty() && candidate.path.last().decodeAs != Encoding::Utf8) value -= 6.0;
    if (partial) value -= 15.0;
    else if (!candidate.reversible) value -= 2.0;
    return std::clamp(value, 0.0, 100.0);
}
}
