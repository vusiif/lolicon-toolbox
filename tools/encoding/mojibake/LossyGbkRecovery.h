#pragma once
#include "MojibakeCandidate.h"
#include <functional>
namespace mojibake {
// How much of a recovered character is backed by surviving bytes.
enum class CharEvidence : char {
    Exact = 'E',    // every byte survived in the damaged text
    Partial = 'P',  // one byte survived; the other was chosen by the word model
    Guided = 'G',   // no byte survived; the character comes from a guide text (AI) that fits the byte pattern
    Unknown = 'U',  // no byte survived; shown as UnknownMark
};
// How a decoder replaced broken UTF-8: one U+FFFD per maximal subpart (WHATWG,
// Python, ICU, Java) or one per byte (Qt, .NET).
enum class Utf8Replacement { MaximalSubpart, PerByte };
// One independently recovered span: damaged[damagedBegin, damagedEnd) became text[textBegin, textEnd).
struct LossySpan { qsizetype damagedBegin = 0, damagedEnd = 0, textBegin = 0, textEnd = 0; };
struct LossyRecovery {
    QString source;  // the damaged text that spans refer to
    QString text;
    QByteArray evidence;  // one CharEvidence per QChar of text
    QByteArray bytes;     // GBK bytes of the reconstruction: one per ASCII character of text, two per other character
    QList<LossySpan> spans;
    int exact = 0, partial = 0, guided = 0, unknown = 0;
    int guideRejected = 0;  // guide characters that contradict the surviving bytes
    bool success = false, cancelled = false;
    Utf8Replacement replacement = Utf8Replacement::MaximalSubpart;  // the convention that explains the damage
    void count();
};
// GBK text that was decoded as UTF-8 with replacement characters and saved
// again, as many editors and phones do. Every byte that formed valid UTF-8
// survives unchanged; each U+FFFD replaced one to three bytes that did not.
// Recovery searches GBK byte strings whose lossy UTF-8 decoding is exactly
// the damaged text and ranks them with a word model built from the jieba
// dictionary (resources/charlm). Characters without any surviving byte cannot
// be recovered from the bytes; they are marked, or taken from a guide.
class LossyGbkRecovery {
public:
    static constexpr char16_t UnknownMark = 0x25a1;  // □
    static constexpr char16_t GuideGap = 0xfffc;     // in a guide: one character the guide does not know (not GBK encodable)
    // Cheap test for the damage pattern: many replacement characters, and the
    // surviving non-ASCII characters are mostly GBK byte pairs read as UTF-8
    // (Latin, Greek, Cyrillic, Hebrew... rather than common CJK).
    static bool applicable(const QString& damaged);
    static bool modelAvailable();
    // progress receives the processed fraction (0..1) from worker threads.
    static LossyRecovery recover(const QString& damaged, const Cancellation& cancel = {},
                                 const std::function<void(double)>& progress = {});
    // The same search, preferring the characters of guide (for example an AI
    // proposal for this span). Guide characters that contradict the surviving
    // bytes are rejected and counted; they never override them. Markup the
    // guide copied back is understood: 〔x|y〕 counts as x, □ as a gap.
    static LossyRecovery guided(const QString& damaged, const QString& guide, Utf8Replacement replacement,
                                const Cancellation& cancel = {});
    static QString lossyUtf8(QByteArrayView bytes, Utf8Replacement replacement = Utf8Replacement::MaximalSubpart);
    // candidate, encoded as GBK and decoded with lossyUtf8, reproduces damaged.
    static bool consistent(const QString& candidate, const QString& damaged,
                           Utf8Replacement replacement = Utf8Replacement::MaximalSubpart);
    // Characters that could stand at text[index] of a partial character: they
    // keep its surviving byte and reproduce the damage, likeliest first by the
    // word model in the surrounding context, at most limit of them.
    static QString alternatives(const LossyRecovery& recovery, qsizetype index, int limit);
    // Text for an AI prompt: exact characters as is, unknown ones as □, and
    // partial ones as 〔x〕, or as 〔x|y|z〕 listing up to options
    // alternatives (likeliest first) where the surviving byte allows several.
    static QString markup(const LossyRecovery& recovery, qsizetype textBegin, qsizetype textEnd, int options = 0);
    // Words and numbers that recur in the document and whose bytes all survived,
    // most telling first, at most maxCharacters in all: clues for filling lost
    // characters elsewhere, never constraints.
    static QStringList glossary(const LossyRecovery& recovery, int maxCharacters);
};
}
