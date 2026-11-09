#pragma once
#include "MojibakeCandidate.h"
namespace mojibake {
class CandidateScorer {
public:
    // Encoding damage evidence; includes rareCharacters().
    static int anomalies(const QString& text);
    // CJK characters outside the common first-level sets of GB2312, Big5, JIS X 0208
    // and KS X 1001, plus halfwidth katakana and compatibility jamo.
    static int rareCharacters(const QString& text);
    // plainCodec: the codec a path-less candidate was decoded with, when known (file detection).
    static double score(const Candidate& candidate, std::optional<Encoding> plainCodec = {});
    // Weak evidence that text was read with the wrong codec (kana from GBK, hanja
    // from EUC-KR, Han-space-Han). Added to the anomaly count by score().
    static double scriptPenalty(const QString& text, std::optional<Encoding> codec);
    // Small penalty that orders equally clean legacy decodings.
    static double legacyPrior(Encoding codec);
};
}
