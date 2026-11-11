#include "CandidateScorer.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
namespace mojibake {
namespace {
// Characters that ordinary CJK text is made of, derived from the codecs' own
// frequency-ordered first levels: GB2312 level 1 (3755 hanzi), Big5 level 1
// (5401 common traditional hanzi), JIS X 0208 level 1 (2965 kanji) and the 2350
// KS X 1001 hangul syllables. Mojibake produced by reading bytes with the wrong
// codec lands mostly outside these sets (for example in GBK's extension rows,
// whose trail bytes are UTF-8 continuation bytes). No external word list is used.
QSet<char32_t> buildCommon() {
    QSet<char32_t> set;
    const auto add = [&set](const char* codec, const int leadFrom, const int leadTo, auto trailOk, auto pairOk) {
        QStringDecoder decoder(codec, QStringConverter::Flag::Stateless);
        for (int lead = leadFrom; lead <= leadTo; ++lead)
            for (int trail = 0x40; trail <= 0xfe; ++trail) {
                if (!trailOk(trail) || !pairOk(lead, trail)) continue;
                const char bytes[2] = {static_cast<char>(lead), static_cast<char>(trail)};
                const QString text = decoder.decode(QByteArrayView(bytes, 2));
                if (decoder.hasError()) { decoder.resetState(); continue; }
                if (text.size() == 1 && text[0] != QChar(0xfffd) && text[0].unicode() >= 0x80) set.insert(text[0].unicode());
            }
    };
    const auto any = [](int, int) { return true; };
    const auto high = [](const int t) { return t >= 0xa1 && t <= 0xfe; };
    add("GBK", 0xb0, 0xd7, high, [](const int lead, const int trail) { return lead < 0xd7 || trail <= 0xf9; });
    add("Big5", 0xa4, 0xc6, [](const int t) { return (t >= 0x40 && t <= 0x7e) || (t >= 0xa1 && t <= 0xfe); },
        [](const int lead, const int trail) { return lead < 0xc6 || trail <= 0x7e; });
    add("Shift_JIS", 0x88, 0x98, [](const int t) { return (t >= 0x40 && t <= 0x7e) || (t >= 0x80 && t <= 0xfc); },
        [](const int lead, const int trail) { return (lead > 0x88 || trail >= 0x9f) && (lead < 0x98 || trail <= 0x72); });
    add("EUC-KR", 0xb0, 0xc8, high, any);
    return set;
}
const QSet<char32_t>& commonCharacters() {
    static const QSet<char32_t> set = buildCommon(); // Thread-safe one-time initialization.
    return set;
}
bool isHan(const char32_t u) {
    return (u >= 0x3400 && u <= 0x4dbf) || (u >= 0x4e00 && u <= 0x9fff) || (u >= 0xf900 && u <= 0xfaff) || (u >= 0x20000 && u <= 0x3134f);
}
bool isKana(const char32_t u) { return u >= 0x3041 && u <= 0x30ff; }
}
int CandidateScorer::rareCharacters(const QString& text) {
    const auto& common = commonCharacters();
    int count = 0;
    // Walk code points in place: this runs for every candidate of inputs up to 10 MiB.
    for (qsizetype i = 0; i < text.size(); ++i) {
        char32_t u = text[i].unicode();
        if (u < 0x3000) continue;
        if (QChar::isHighSurrogate(u) && i + 1 < text.size() && text[i + 1].isLowSurrogate())
            u = QChar::surrogateToUcs4(static_cast<char16_t>(u), text[++i].unicode());
        // Halfwidth katakana and compatibility jamo are almost always codec debris in modern text.
        if ((u >= 0xff61 && u <= 0xff9f) || (u >= 0x3131 && u <= 0x318e)) ++count;
        else if ((isHan(u) || (u >= 0xac00 && u <= 0xd7a3)) && !common.contains(u)) ++count;
    }
    return count;
}
int CandidateScorer::anomalies(const QString& text) {
    int count = 0;
    for (auto c : text) {
        if (const auto u = c.unicode(); u == 0xfffd) count += 8;
        else if (u >= 0xe000 && u <= 0xf8ff) ++count;
        else if ((u < 32 && c != '\n' && c != '\r' && c != '\t') || (u >= 0x7f && u < 0xa0)) count += 3;
    }
    // Multi-character evidence, not a blanket penalty for accented/CJK letters:
    // a UTF-8 lead byte followed by a continuation byte as read by Latin-1 or
    // Windows-1252 (whose 0x80-0x9F row is listed explicitly), UTF-8 CJK punctuation
    // read as GBK/Shift-JIS, the classic GBK rendering of replacement characters,
    // and runs of Latin-1 symbols or three or more accented letters in a row, which
    // real European words do not contain.
    static const QRegularExpression patterns(QString::fromUtf8(
        "[\\x{00C2}-\\x{00F4}][\\x{0080}-\\x{00BF}\\x{20AC}\\x{201A}\\x{0192}\\x{201E}\\x{2026}\\x{2020}\\x{2021}"
        "\\x{02C6}\\x{2030}\\x{0160}\\x{2039}\\x{0152}\\x{017D}\\x{2018}\\x{2019}\\x{201C}\\x{201D}\\x{2022}\\x{2013}"
        "\\x{2014}\\x{02DC}\\x{2122}\\x{0161}\\x{203A}\\x{0153}\\x{017E}\\x{0178}]"
        "|銆[傘備]|鈥[檚滙]|縺[薙繧]|锟斤拷|[\\x{00A0}-\\x{00BF}]{2,}|[\\x{00C0}-\\x{00FF}]{3,}"));
    auto matches = patterns.globalMatch(text);
    while (matches.hasNext()) { matches.next(); ++count; }
    return count + rareCharacters(text);
}
double CandidateScorer::score(const Candidate& candidate, const std::optional<Encoding> plainCodec) {
    double bad = anomalies(candidate.text);
    const bool partial = std::ranges::any_of(candidate.path, [](const auto& step) { return step.partial; });
    // A marked hole in a partial repair is expected missing information, not
    // eight new encoding errors. Keep a separate penalty for incomplete repair.
    if (partial) bad -= 7.0 * candidate.text.count(QChar(0xfffd));
    if (!candidate.path.isEmpty()) bad += scriptPenalty(candidate.text, candidate.path.last().decodeAs);
    else bad += scriptPenalty(candidate.text, plainCodec);
    // Normalize by length: repeating a paragraph must not make its quality
    // arbitrarily negative. This is a quality index, never a probability.
    const double length = std::max<qsizetype>(8, candidate.text.size());
    double value = 100.0 / (1.0 + 4.0 * bad / length);
    value -= 1.5 * candidate.path.size();
    // Equally clean legacy decodings often produce unrelated CJK glyphs.
    // Keep the UTF-8 prior explicit, below the anomaly and damage penalties.
    if (!candidate.path.isEmpty())
        if (const auto codec = candidate.path.last().decodeAs; codec != Encoding::Utf8) value -= 6.0 + legacyPrior(codec);
    if (partial) value -= 15.0;
    else if (!candidate.reversible) value -= 2.0;
    return std::clamp(value, 0.0, 100.0);
}
double CandidateScorer::scriptPenalty(const QString& text, const std::optional<Encoding> codec) {
    const auto ucs = text.toUcs4();
    double bad = 0;
    // Script that the codec is not normally used for: kana from Chinese codecs,
    // hanja from EUC-KR. Each such character is weak evidence of a wrong reading.
    if (codec == Encoding::Gbk || codec == Encoding::Gb18030 || codec == Encoding::Big5)
        bad += 0.5 * std::ranges::count_if(ucs, isKana);
    else if (codec == Encoding::EucKr)
        bad += 0.5 * std::ranges::count_if(ucs, isHan);
    // Chinese and Japanese do not separate words with spaces; Han-space-Han is
    // typical of Korean byte runs read as a Chinese codec.
    for (qsizetype i = 0; i + 2 < ucs.size(); ++i)
        if (isHan(ucs[i]) && ucs[i + 1] == U' ' && isHan(ucs[i + 2])) bad += 0.5;
    return bad;
}
double CandidateScorer::legacyPrior(const Encoding codec) {
    // Tie-break between legacy codecs whose decodings are equally clean, such as
    // a two-character GBK word that is also valid Big5. Simplified Chinese is the
    // primary audience; the gaps are smaller than one rare character's penalty.
    switch (codec) {
    case Encoding::Gbk: return 0.0;
    case Encoding::Gb18030: return 0.1;
    case Encoding::Big5: return 0.3;
    case Encoding::ShiftJis: return 0.4;
    case Encoding::EucKr: return 0.5;
    default: return 0.6;
    }
}
}
