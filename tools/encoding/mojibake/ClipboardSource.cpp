#include "ClipboardSource.h"
#include <QMimeData>
#include <QTextDocument>
#include <QUrl>
namespace mojibake {
namespace {
bool tooLarge(const QString& text) { return text.size() > ClipboardSource::MaxBytes || text.toUtf8().size() > ClipboardSource::MaxBytes; }
class TextOnlyDocument final : public QTextDocument {
protected:
    QVariant loadResource(int, const QUrl&) override { return {}; }
};
}
bool ClipboardTexts::plainUsable() const {
    return capture.hasPlain && !capture.plainTooLarge && !capture.plainInvalid && !capture.plain.isEmpty();
}
bool ClipboardTexts::htmlUsable() const {
    return capture.hasHtml && !capture.htmlTooLarge && !capture.htmlInvalid && !htmlTextTooLarge && !htmlTextInvalid && !htmlText.isEmpty();
}
ClipboardCapture ClipboardSource::capture(const QMimeData& mime) {
    ClipboardCapture result; result.formats = mime.formats();
    result.hasPlain = mime.hasText(); result.hasHtml = mime.hasHtml();
    for (const auto& format : result.formats)
        if (format.contains("rtf", Qt::CaseInsensitive) || format.contains("Rich Text Format", Qt::CaseInsensitive)) result.hasRtf = true;
    if (result.hasPlain) {
        result.plain = mime.text(); result.plainInvalid = !result.plain.isValidUtf16(); result.plainTooLarge = tooLarge(result.plain);
        if (result.plainInvalid || result.plainTooLarge) result.plain.clear();
    }
    if (result.hasHtml) {
        result.html = mime.html(); result.htmlInvalid = !result.html.isValidUtf16(); result.htmlTooLarge = tooLarge(result.html);
        if (result.htmlInvalid || result.htmlTooLarge) result.html.clear();
    }
    return result;
}
ClipboardTexts ClipboardSource::extract(ClipboardCapture capture) {
    ClipboardTexts result; result.capture = std::move(capture);
    if (result.capture.hasHtml && !result.capture.htmlTooLarge && !result.capture.htmlInvalid && !result.capture.html.isEmpty()) {
        TextOnlyDocument document;
        // Disable both the document's loader and resource-provider fallback.
        document.setResourceProvider([](const QUrl&) { return QVariant(); });
        document.setHtml(result.capture.html);
        result.htmlText = document.toRawText().replace(QChar::ParagraphSeparator, '\n').replace(QChar::LineSeparator, '\n');
        result.htmlTextTooLarge = tooLarge(result.htmlText); result.htmlTextInvalid = !result.htmlText.isValidUtf16();
        if (result.htmlTextTooLarge || result.htmlTextInvalid) result.htmlText.clear();
    }
    // Retain source text, not executable/rendered HTML or external resources.
    result.capture.html.clear();
    return result;
}
}
