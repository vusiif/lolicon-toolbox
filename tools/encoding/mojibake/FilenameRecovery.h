#pragma once
#include "MojibakeCandidate.h"
#include <QStringList>
namespace mojibake {
struct NameEntry {
    QString relativePath, selectedName;
    int parent = -1;
    bool directory = false;
    qint64 size = 0;
    QByteArray hash;
    QList<Candidate> candidates;
};
struct NamePlan {
    QString root, error;
    QList<NameEntry> entries;
    qint64 totalBytes = 0;
    bool cancelled = false;
};
struct NameValidation {
    QStringList paths, errors;
    bool valid() const;
};
struct NameCopyResult {
    bool success = false, cancelled = false;
    QString destination, error;
    bool outputCreated = false;
};
struct NameProgress { std::atomic_int entries{0}; std::atomic<qint64> bytes{0}; };
using NameProgressPtr = std::shared_ptr<NameProgress>;
class FilenameRecovery {
public:
    static constexpr int MaxEntries = 1000, MaxDepth = 32;
    static constexpr qint64 MaxTotalBytes = 256 * 1024 * 1024;
    static NamePlan scan(const QString& root, const Cancellation& cancel = {}, const NameProgressPtr& progress = {});
    static QString invalidName(const QString& name);
    static NameValidation validate(const NamePlan& plan);
    static NameCopyResult copy(const NamePlan& plan, const QString& destination,
                               const Cancellation& cancel = {}, const NameProgressPtr& progress = {});
};
}
