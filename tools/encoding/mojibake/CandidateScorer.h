#pragma once
#include "MojibakeCandidate.h"
namespace mojibake {
class CandidateScorer {
public:
    static int anomalies(const QString& text);
    static double score(const Candidate& candidate);
};
}
