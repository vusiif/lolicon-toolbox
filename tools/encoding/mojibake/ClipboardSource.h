#pragma once
#include <QStringList>
class QMimeData;
namespace mojibake {
struct ClipboardCapture {
    QStringList formats;
    QString plain, html;
    bool hasPlain = false, hasHtml = false, hasRtf = false;
    bool plainTooLarge = false, htmlTooLarge = false;
    bool plainInvalid = false, htmlInvalid = false;
};
struct ClipboardTexts {
    ClipboardCapture capture;
    QString htmlText;
    bool htmlTextTooLarge = false, htmlTextInvalid = false;
    bool plainUsable() const;
    bool htmlUsable() const;
};
class ClipboardSource {
public:
    static constexpr qsizetype MaxBytes = 1024 * 1024;
    // Capture lazily offered text on the GUI thread; never retain QMimeData.
    static ClipboardCapture capture(const QMimeData& mime);
    static ClipboardTexts extract(ClipboardCapture capture);
};
}
