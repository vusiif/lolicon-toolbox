#include "CandidateGenerator.h"
#include "CandidateScorer.h"
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
namespace mojibake {
std::optional<QString> CandidateGenerator::transform(const QString& text, const Transformation& step) {
    if (!step.segmented) {
        const auto bytes = EncodingConverter::encode(text, step.encodeAs);
        if (!bytes) return {};
        return EncodingConverter::decode(*bytes, step.decodeAs);
    }
    // Preserve other scripts verbatim; repair complete representable Western runs.
    static const QRegularExpression runs("[\\x{0000}-\\x{00FF}\\x{0152}\\x{0153}\\x{0160}\\x{0161}\\x{0178}\\x{017D}\\x{017E}\\x{0192}\\x{02C6}\\x{02DC}\\x{2013}-\\x{2022}\\x{2026}\\x{2030}\\x{2039}\\x{203A}\\x{20AC}\\x{2122}]+");
    auto matches = runs.globalMatch(text);
    QString result;
    qsizetype previous = 0;
    bool changed = false;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += text.mid(previous, match.capturedStart() - previous);
        const auto original = match.captured();
        const auto bytes = EncodingConverter::encode(original, step.encodeAs);
        const auto decoded = bytes ? EncodingConverter::decode(*bytes, step.decodeAs) : std::nullopt;
        if (decoded && *decoded != original) { result += *decoded; changed = true; }
        else result += original;
        previous = match.capturedEnd();
    }
    result += text.mid(previous);
    if (!changed) return {};
    return result;
}
QList<Candidate> CandidateGenerator::generate(const QString& text, const Cancellation& cancel, bool& limited) {
    Candidate original{text, {}};
    original.score = CandidateScorer::score(original);
    original.anomalies = CandidateScorer::anomalies(text);
    QList<Candidate> all{original}, frontier{original};
    QSet<QString> seen{text};
    const auto encodings = EncodingConverter::supported();
    for (int depth = 0; depth < 2; ++depth) {
        QList<Candidate> next;
        for (const auto& parent : frontier) {
            for (auto from : encodings) {
                if (cancel && cancel->load()) return all;
                const auto bytes = EncodingConverter::encode(parent.text, from);
                for (auto to : encodings) {
                    if (cancel && cancel->load()) return all;
                    if (from == to) continue;
                    Transformation step{from, to};
                    auto transformed = bytes ? EncodingConverter::decode(*bytes, to) : std::nullopt;
                    if (!transformed && to == Encoding::Utf8 && (from == Encoding::Latin1 || from == Encoding::Windows1252)) {
                        step.segmented = true;
                        transformed = transform(parent.text, step);
                    }
                    if (!transformed || seen.contains(*transformed)) continue;
                    Candidate candidate{*transformed, parent.path};
                    candidate.path.append(step);
                    // Every whole-string step is exact. For segmented repairs,
                    // invert with a whole-string encode/decode and verify equality.
                    if (step.segmented) {
                        const auto inverse = transform(candidate.text, {to, from});
                        // Mixed-script inverses need to preserve the unchanged runs.
                        // If not provable, retain candidate with an honest flag.
                        candidate.reversible = inverse && *inverse == parent.text;
                    }
                    candidate.reversible = candidate.reversible && parent.reversible;
                    candidate.score = CandidateScorer::score(candidate);
                    candidate.anomalies = CandidateScorer::anomalies(candidate.text);
                    seen.insert(candidate.text);
                    next.append(candidate);
                    all.append(candidate);
                    if (all.size() >= 100) { limited = true; return all; }
                }
            }
        }
        std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
        if (next.size() > 12) { next = next.mid(0, 12); limited = true; }
        frontier = next;
        if (frontier.isEmpty()) break;
    }
    return all;
}
}
