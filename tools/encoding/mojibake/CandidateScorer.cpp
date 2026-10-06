#include "CandidateScorer.h"
#include <QRegularExpression>
#include <algorithm>
namespace mojibake {
int CandidateScorer::anomalies(const QString& text) {
    int count = 0;
    for (auto c : text) {
        const auto u = c.unicode();
        if (u == 0xfffd) count += 8;
        else if ((u < 32 && c != '\n' && c != '\r' && c != '\t') || (u >= 0x7f && u < 0xa0)) count += 3;
    }
    // Multi-character evidence, not a blanket penalty for accented/CJK letters.
    static const QRegularExpression patterns(QString::fromUtf8(
        "[\\x{00C2}-\\x{00F4}][\\x{0080}-\\x{00BF}€™œžšŸ]|浣犲|銆[傘備]|鈥[檚滙]|縺[薙繧]|锟斤拷|[\\x{00A0}-\\x{00BF}]{2,}"));
    auto matches = patterns.globalMatch(text);
    while (matches.hasNext()) { matches.next(); ++count; }
    return count;
}
double CandidateScorer::score(const Candidate& candidate) {
    const int bad = anomalies(candidate.text);
    // Scores are heuristics, never probabilities. Preserve plausible originals.
    double value = 80.0 - std::min(65.0, 20.0 * bad) - candidate.path.size() * 1.5;
    if (candidate.path.isEmpty()) value += 2.0;
    else if (candidate.path.last().decodeAs == Encoding::Utf8) value += 10.0;
    if (candidate.text.contains(QChar(0xfffd))) value -= 20;
    return value;
}
}
