#pragma once
#include "ByteEncodingAnalyzer.h"
#include "FilenameRecovery.h"
#include "ClipboardSource.h"
#include <QJsonObject>
namespace mojibake {
struct DiagnosticReport {
    QJsonObject data;
    bool ready = false;
    QString summary() const;
    static DiagnosticReport text(const Result& result, int selected);
    static DiagnosticReport file(const FileResult& result, int selected);
    static DiagnosticReport names(const NamePlan& plan);
    static DiagnosticReport clipboard(const ClipboardTexts& snapshot, int selected);
    bool save(const QString& destination, QString& error) const;
};
}
