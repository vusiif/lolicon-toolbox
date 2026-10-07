#include "ClipboardSourcePage.h"
#include "DiagnosticReportWidget.h"
#include "CandidateScorer.h"
#include "UiTheme.h"
#include <ElaScrollArea.h>
#include <ElaPushButton.h>
#include <ElaPlainTextEdit.h>
#include <ElaTheme.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QEvent>
#include <QtConcurrentRun>
using namespace mojibake;
namespace {
constexpr qsizetype PreviewLimit = 32768;
QString preview(const QString& text) {
    auto size = std::min(text.size(), PreviewLimit);
    if (size < text.size() && size && text[size - 1].isHighSurrogate()) --size;
    return text.left(size);
}
}
ClipboardSourcePage::ClipboardSourcePage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content = new QWidget(scroll); content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false); scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(32, 28, 32, 28); layout->setSpacing(12);
    m_title = ui::label({}, 18, ui::Tone::Primary, content); m_description = ui::label({}, 10, ui::Tone::Secondary, content);
    m_status = ui::label({}, 10, ui::Tone::Secondary, content); m_status->setTextFormat(Qt::PlainText); m_status->setObjectName("clipboardStatus");
    auto button = [content](const char* name) { auto* b = new ElaPushButton(content); b->setObjectName(name); return b; };
    m_read = button("clipboardRead"); ui::primaryButton(m_read); m_clear = button("clipboardClear");
    m_plain = button("clipboardPlain"); m_html = button("clipboardHtml"); m_use = button("clipboardUse");
    m_plain->setCheckable(true); m_html->setCheckable(true);
    auto edit = [content](const char* name, int height) {
        auto* widget = new ElaPlainTextEdit(content); widget->setObjectName(name); widget->setReadOnly(true); widget->setMinimumHeight(height); return widget;
    };
    m_formats = edit("clipboardFormats", 80); m_formats->setMaximumHeight(110);
    m_plainPreview = edit("clipboardPlainPreview", 150); m_htmlPreview = edit("clipboardHtmlPreview", 150);
    m_details = edit("clipboardDetails", 140);
    m_report = new DiagnosticReportWidget(content); m_report->setObjectName("clipboardDiagnosticReport");
    auto* actions = new QHBoxLayout; actions->addWidget(m_read); actions->addWidget(m_clear); actions->addStretch();
    auto* sources = new QHBoxLayout;
    auto* plainColumn = new QVBoxLayout; plainColumn->addWidget(m_plain); plainColumn->addWidget(m_plainPreview);
    auto* htmlColumn = new QVBoxLayout; htmlColumn->addWidget(m_html); htmlColumn->addWidget(m_htmlPreview);
    sources->addLayout(plainColumn, 1); sources->addLayout(htmlColumn, 1);
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addLayout(actions); layout->addWidget(m_status);
    layout->addWidget(m_formats); layout->addLayout(sources); layout->addWidget(m_report); layout->addWidget(m_details); layout->addWidget(m_use, 0, Qt::AlignLeft); layout->addStretch();
    connect(m_read, &ElaPushButton::clicked, this, [this] {
        const auto* mime = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
        if (mime) inspect(*mime);
        else { QMimeData empty; inspect(empty); }
    });
    connect(m_plain, &ElaPushButton::clicked, this, [this] { m_selected = 0; refresh(); });
    connect(m_html, &ElaPushButton::clicked, this, [this] { m_selected = 1; refresh(); });
    connect(m_clear, &ElaPushButton::clicked, this, [this] { m_snapshot = {}; m_readOnce = false; m_selected = -1; refresh(); });
    connect(m_use, &ElaPushButton::clicked, this, [this] {
        if (m_worker.isRunning()) return;
        if (m_selected == 0 && m_snapshot.plainUsable()) emit textChosen(m_snapshot.capture.plain);
        else if (m_selected == 1 && m_snapshot.htmlUsable()) emit textChosen(m_snapshot.htmlText);
    });
    connect(&m_worker, &QFutureWatcher<ClipboardTexts>::finished, this, [this] {
        m_snapshot = m_worker.result(); m_selected = m_snapshot.plainUsable() ? 0 : m_snapshot.htmlUsable() ? 1 : -1; refresh();
    });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { refresh(); });
    retranslate();
}
void ClipboardSourcePage::inspect(const QMimeData& mime) {
    if (m_worker.isRunning()) return;
    m_readOnce = true; m_snapshot = {}; m_selected = -1;
    auto capture = ClipboardSource::capture(mime);
    m_worker.setFuture(QtConcurrent::run([capture = std::move(capture)]() mutable { return ClipboardSource::extract(std::move(capture)); }));
    refresh();
}
void ClipboardSourcePage::refresh() {
    const bool busy = m_worker.isRunning(); const auto& capture = m_snapshot.capture;
    m_report->setReport(busy || !m_readOnce ? DiagnosticReport{} : DiagnosticReport::clipboard(m_snapshot, m_selected));
    m_read->setEnabled(!busy); m_clear->setEnabled(!busy && m_readOnce);
    m_plain->setEnabled(!busy && m_snapshot.plainUsable()); m_html->setEnabled(!busy && m_snapshot.htmlUsable());
    m_plain->setChecked(m_selected == 0); m_html->setChecked(m_selected == 1); m_use->setEnabled(!busy && m_selected >= 0);
    for (auto* b : {m_plain, m_html}) {
        const bool selected = b->isChecked();
        for (auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
            const auto base = eTheme->getThemeColor(mode, selected ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase);
            const auto hover = eTheme->getThemeColor(mode, selected ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover);
            const auto press = eTheme->getThemeColor(mode, selected ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress);
            const auto color = eTheme->getThemeColor(mode, selected ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText);
            if (mode == ElaThemeType::Light) { b->setLightDefaultColor(base); b->setLightHoverColor(hover); b->setLightPressColor(press); b->setLightTextColor(color); }
            else { b->setDarkDefaultColor(base); b->setDarkHoverColor(hover); b->setDarkPressColor(press); b->setDarkTextColor(color); }
        }
        b->update();
    }
    if (busy) m_status->setText(tr("Extracting clipboard text locally…"));
    else if (!m_readOnce) m_status->setText(tr("Read the clipboard explicitly. It is not monitored automatically."));
    else if (m_selected < 0) m_status->setText(tr("No usable text representation. Check the format and limit notes below."));
    else if (m_snapshot.plainUsable() && m_snapshot.htmlUsable() && capture.plain != m_snapshot.htmlText)
        m_status->setText(tr("The two representations differ. Compare them; formatting differences can also explain this."));
    else m_status->setText(tr("Select a representation, then use it in the text analysis page."));
    m_formats->setPlainText(capture.formats.isEmpty() ? tr("No advertised formats in this snapshot.") : tr("Advertised formats (not all are decoded):") + '\n' + capture.formats.join('\n'));
    m_plainPreview->setPlainText(preview(capture.plain)); m_htmlPreview->setPlainText(preview(m_snapshot.htmlText));
    QStringList details;
    if (m_readOnce && !busy) {
        auto describe = [this, &details](const QString& label, const QString& text, bool present, bool oversized, bool invalid) {
            if (!present) details << tr("%1: not provided.").arg(label);
            else if (oversized) details << tr("%1: exceeds the 1 MiB limit; not loaded.").arg(label);
            else if (invalid) details << tr("%1: invalid Unicode; not loaded.").arg(label);
            else details << tr("%1: %2 UTF-16 units, %3 anomalies, %4 replacement characters.").arg(label).arg(text.size()).arg(CandidateScorer::anomalies(text)).arg(text.count(QChar(0xfffd)));
        };
        describe(tr("Plain text"), capture.plain, capture.hasPlain, capture.plainTooLarge, capture.plainInvalid);
        describe(tr("HTML-extracted text"), m_snapshot.htmlText, capture.hasHtml, capture.htmlTooLarge || m_snapshot.htmlTextTooLarge, capture.htmlInvalid || m_snapshot.htmlTextInvalid);
        if (m_snapshot.plainUsable() && m_snapshot.htmlUsable() && capture.plain == m_snapshot.htmlText) details << tr("Both representations contain identical Unicode text.");
        if (capture.hasRtf) details << tr("RTF is advertised but is not parsed in this stage.");
        if (capture.plain.contains(QChar(0xfffd)) || m_snapshot.htmlText.contains(QChar(0xfffd))) details << tr("Possible information loss. A replacement character does not identify the missing original bytes.");
        if (m_snapshot.htmlText.contains(QChar::ObjectReplacementCharacter)) details << tr("HTML contains non-text objects. Their placeholders are retained; images are not loaded.");
        if (capture.plain.size() > PreviewLimit || m_snapshot.htmlText.size() > PreviewLimit) details << tr("Previews show at most 32,768 UTF-16 units. Using a representation transfers its complete text.");
    }
    details << tr("Clipboard text is decoded Unicode, not proven original file bytes. HTML may offer different text but does not prove the original meaning.");
    details << tr("HTML is extracted as text without loading external resources. Unknown formats are not read. AI is only called separately from the text analysis page.");
    m_details->setPlainText(details.join('\n'));
}
void ClipboardSourcePage::retranslate() {
    m_title->setText(tr("Clipboard source analysis"));
    m_description->setText(tr("Read one snapshot and compare plain text with HTML-extracted text. Choose the text to analyze; reading does not upload it or replace your current input."));
    m_read->setText(tr("Read clipboard")); m_clear->setText(tr("Clear snapshot")); m_plain->setText(tr("Plain text")); m_html->setText(tr("HTML-extracted text")); m_use->setText(tr("Use selected text")); refresh();
}
void ClipboardSourcePage::changeEvent(QEvent* event) { QWidget::changeEvent(event); if (event->type() == QEvent::LanguageChange) retranslate(); }
