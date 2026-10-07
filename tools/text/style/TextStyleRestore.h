#pragma once
#include "TextStyleEngine.h"
#include <QJsonArray>
namespace textstyle {
struct Restoration {
    Error error = Error::None;
    QStringList candidates;
    QJsonArray ambiguities;
    uint64_t matched = 0, ambiguous = 0, wrappers = 0;
    bool success() const { return error == Error::None; }
};
class Restore {
public:
    static Restoration run(const QString&, std::shared_ptr<const Definition>, bool removeWrappers = true, const Control& = {});
};
}
