#include "CandidateGenerator.h"
#include "CandidateScorer.h"
#include <QSet>
#include <QRegularExpression>
#include <QStringConverter>
#include <algorithm>
namespace mojibake {
namespace {
// Reconstruct bytes up to each damaged character. A decoder must never carry
// an incomplete UTF-8 sequence across a hole in the original byte stream.
std::optional<QString> partialUtf8(const QString& text, const Encoding from, const Cancellation& cancel = {}) {
    QString result;
    QByteArray run;
    qsizetype decodedCount = 0, invalidCount = 0, recoveredNonAscii = 0;
    auto flush = [&] {
        if (run.isEmpty()) return;
        QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        const QString decoded = decoder(run);
        decodedCount += decoded.size();
        for (auto c : decoded) {
            if (c == QChar(0xfffd)) ++invalidCount;
            else if (c.unicode() >= 0x80) ++recoveredNonAscii;
        }
        result += decoded;
        run.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (i % 256 == 0 && cancel && cancel->load()) return {};
        const auto length = text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate() ? 2 : 1;
        const auto character = text.mid(i, length);
        i += length - 1;
        if (const auto bytes = character.contains(QChar(0xfffd)) ? std::nullopt : EncodingConverter::encode(character, from)) run += *bytes;
        else { flush(); result += character; }
    }
    flush();
    // Collapsing unknown regions alone is not evidence of recovery. Require
    // actual non-ASCII UTF-8 and mostly valid decoding before offering a result.
    if (recoveredNonAscii < 2 || invalidCount * 4 > decodedCount) return {};
    // Adjacent replacement characters describe one unknown region; do not
    // imply how many original characters it contained.
    result.replace(QRegularExpression("\\x{FFFD}+"), QString(QChar(0xfffd)));
    // Only offer a partial recovery when there is measurable evidence of repair.
    if (result == text || CandidateScorer::anomalies(result) >= CandidateScorer::anomalies(text)) return {};
    return result;
}
}
std::optional<QString> CandidateGenerator::transform(const QString& text, const Transformation& step) {
    if (step.partial) return partialUtf8(text, step.encodeAs);
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
        if (const auto decoded = bytes ? EncodingConverter::decode(*bytes, step.decodeAs) : std::nullopt; decoded && *decoded != original) {
            result += *decoded; changed = true;
        }
        else result += original;
        previous = match.capturedEnd();
    }
    result += text.mid(previous);
    if (!changed) return {};
    return result;
}
QList<Candidate> CandidateGenerator::generate(const QString& text, const Cancellation& cancel, bool& limited) {
    Candidate original{.text = text, .path = {}};
    original.score = CandidateScorer::score(original);
    original.anomalies = CandidateScorer::anomalies(text);
    QList<Candidate> all{original}, frontier{original};
    QSet seen{text};
    // UTF-16/32 byte detection belongs to the file analyzer. Adding them to
    // Unicode-string inversion would manufacture unrelated wide-codec text.
    const auto encodings = EncodingConverter::supported(false);
    for (int depth = 0; depth < 2; ++depth) {
        QList<Candidate> next;
        for (const auto& parent : frontier) {
            if (std::ranges::any_of(parent.path, [](const auto& step) { return step.partial; })) continue;
            const auto cjk = std::ranges::count_if(parent.text, [](QChar c) {
                return c.unicode() >= 0x3400 && c.unicode() <= 0x9fff;
            });
            for (auto from : encodings) {
                if (cancel && cancel->load()) return all;
                // Encoding predominantly CJK text as UTF-8 and reading it as
                // a legacy codec creates fresh mojibake, rather than repairs it.
                if (from == Encoding::Utf8 && cjk > parent.text.size() / 2) continue;
                const auto bytes = EncodingConverter::encode(parent.text, from);
                for (auto to : encodings) {
                    if (cancel && cancel->load()) return all;
                    if (from == to) continue;
                    // A single-byte rendering of CJK bytes can hide damage
                    // markers while destroying the readable text.
                    if (cjk > parent.text.size() / 2 && (to == Encoding::Latin1 || to == Encoding::Windows1252)) continue;
                    // GB18030 writes characters outside GBK as four-byte codes of
                    // digits and 0x81-0x84; reading those as a single-byte codec
                    // yields clean-looking noise ("ƒ2î8"), never a plausible history.
                    if (from == Encoding::Gb18030 && (to == Encoding::Latin1 || to == Encoding::Windows1252)) continue;
                    Transformation step{.encodeAs = from, .decodeAs = to};
                    auto transformed = bytes ? EncodingConverter::decode(*bytes, to) : std::nullopt;
                    if (!transformed && to == Encoding::Utf8 && (from == Encoding::Latin1 || from == Encoding::Windows1252)) {
                        step.segmented = true;
                        transformed = transform(parent.text, step);
                    }
                    if (!transformed && to == Encoding::Utf8 &&
                        (from == Encoding::Gbk || from == Encoding::Gb18030) &&
                        CandidateScorer::anomalies(parent.text) > 0) {
                        step.partial = true;
                        transformed = partialUtf8(parent.text, from, cancel);
                    }
                    if (!transformed || seen.contains(*transformed)) continue;
                    Candidate candidate{.text = *transformed, .path = parent.path};
                    candidate.path.append(step);
                    candidate.anomalies = CandidateScorer::anomalies(candidate.text);
                    if (candidate.anomalies > parent.anomalies) continue;
                    // Every whole-string step is exact. For segmented repairs,
                    // invert with a whole-string encode/decode and verify equality.
                    if (step.segmented) {
                        const auto inverse = transform(candidate.text, {.encodeAs = to, .decodeAs = from});
                        // Mixed-script inverses need to preserve the unchanged runs.
                        // If not provable, retain candidate with an honest flag.
                        candidate.reversible = inverse && *inverse == parent.text;
                    }
                    candidate.reversible = candidate.reversible && parent.reversible;
                    if (step.partial) candidate.reversible = false;
                    candidate.score = CandidateScorer::score(candidate);
                    seen.insert(candidate.text);
                    next.append(candidate);
                    all.append(candidate);
                    if (all.size() >= 100) { limited = true; return all; }
                }
            }
        }
        std::ranges::stable_sort(next, [](const auto& a, const auto& b) { return a.score > b.score; });
        if (next.size() > 12) { next = next.mid(0, 12); limited = true; }
        frontier = next;
        if (frontier.isEmpty()) break;
    }
    return all;
}
}
