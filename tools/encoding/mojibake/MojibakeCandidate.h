#pragma once
#include "EncodingConverter.h"
#include <QList>
#include <atomic>
#include <memory>
namespace mojibake {
struct Transformation { Encoding encodeAs; Encoding decodeAs; bool segmented = false; };
struct Candidate {
    QString text;
    QList<Transformation> path;
    double score = 0;
    int anomalies = 0;
    bool reversible = true;
};
enum class Confidence { High, Medium, Low, Unknown };
struct Result {
    QString original;
    QList<Candidate> candidates;
    Confidence confidence = Confidence::Unknown;
    bool likelyMojibake = false;
    bool possibleLoss = false;
    bool cancelled = false;
    bool tooLarge = false;
    bool searchLimited = false;
    int generated = 0;
};
using Cancellation = std::shared_ptr<std::atomic_bool>;
}
