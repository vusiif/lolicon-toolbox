#include "VoiceDesignerPage.h"
#include "services/AppSettings.h"
#include "UiTheme.h"
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTextDocument>

VoiceDesignerPage::VoiceDesignerPage(QWidget* parent, AppSettings* settings)
    : ToolPage(parent), m_settings(settings), m_service(new speech::MiMoTtsService(this)) {
    setObjectName("voiceDesignerPage");
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content = new QWidget(scroll); scroll->setWidget(content);
    auto backdrop = [this, scroll, content] {
        auto palette = qApp->palette();
        const auto background = eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::WindowBase);
        palette.setColor(QPalette::Window, background); palette.setColor(QPalette::Base, background);
        for (auto* widget : {static_cast<QWidget*>(this), content, scroll->viewport()}) {
            widget->setPalette(palette); widget->setAutoFillBackground(true);
        }
    };
    connect(eTheme, &ElaTheme::themeModeChanged, this, backdrop); backdrop();
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(32, 28, 32, 28); layout->setSpacing(12);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_title = ui::label({}, 18, ui::Tone::Primary, content);
    m_hint = ui::label({}, 10, ui::Tone::Secondary, content); m_hint->setWordWrap(true);
    m_connection = ui::label({}, 10, ui::Tone::Secondary, content); m_connection->setWordWrap(true);
    m_connection->setObjectName("voiceConnection"); m_connection->setTextFormat(Qt::PlainText);
    m_descriptionLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_previewLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    const auto editor = [content](const char* name) {
        auto* widget = new ElaPlainTextEdit(content); widget->setObjectName(name);
        widget->setMinimumHeight(180); widget->setMinimumWidth(0);
        return widget;
    };
    m_description = editor("voiceDescription"); m_preview = editor("voicePreviewText");
    m_editors = new QBoxLayout(QBoxLayout::LeftToRight);
    auto* description = new QVBoxLayout; description->addWidget(m_descriptionLabel); description->addWidget(m_description);
    auto* preview = new QVBoxLayout; preview->addWidget(m_previewLabel); preview->addWidget(m_preview);
    m_editors->addLayout(description, 1); m_editors->addLayout(preview, 1);
    const auto button = [content](const char* name) { auto* widget = new ElaPushButton(content); widget->setObjectName(name); return widget; };
    m_generate = button("voiceGenerate"); ui::primaryButton(m_generate);
    m_cancel = button("voiceCancel"); m_clear = button("voiceClear");
    auto* actions = new QHBoxLayout; actions->addWidget(m_generate); actions->addWidget(m_cancel); actions->addWidget(m_clear); actions->addStretch();
    m_status = ui::label({}, 10, ui::Tone::Secondary, content); m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText); m_status->setObjectName("voiceStatus");
    layout->addWidget(m_title); layout->addWidget(m_hint); layout->addWidget(m_connection);
    layout->addLayout(m_editors); layout->addLayout(actions); layout->addWidget(m_status); layout->addStretch();
    connect(m_description, &ElaPlainTextEdit::textChanged, this, &VoiceDesignerPage::invalidate);
    connect(m_preview, &ElaPlainTextEdit::textChanged, this, &VoiceDesignerPage::invalidate);
    connect(m_generate, &ElaPushButton::clicked, this, &VoiceDesignerPage::generate);
    connect(m_cancel, &ElaPushButton::clicked, this, &VoiceDesignerPage::cancel);
    connect(m_clear, &ElaPushButton::clicked, this, [this] {
        const QSignalBlocker description(m_description), preview(m_preview);
        m_description->clear(); m_preview->clear(); m_result.reset(); invalidate();
    });
    if (settings) connect(settings, &AppSettings::aiConfigurationChanged, this, &VoiceDesignerPage::invalidate);
    connect(m_service, &speech::MiMoTtsService::completed, this, [this](const speech::VoiceDesignResult& result) {
        if (result.snapshot.revision != m_revision) return;
        m_result = result; m_state = State::Success; m_timer.stop(); refresh();
    });
    connect(m_service, &speech::MiMoTtsService::failed, this, [this](speech::VoiceDesignError, const QString& error, quint64 revision) {
        if (revision != m_revision) return;
        m_error = error; m_state = State::Error; m_timer.stop(); refresh();
    });
    connect(m_service, &speech::MiMoTtsService::cancelled, this, [this](quint64 revision) {
        if (revision != m_revision) return;
        m_state = State::Cancelled; m_timer.stop(); refresh();
    });
    m_timer.setInterval(500); connect(&m_timer, &QTimer::timeout, this, &VoiceDesignerPage::refresh);
    retranslate(); refresh();
}
speech::VoiceDesignRequest VoiceDesignerPage::input() const {
    // toRawText retains NBSP; paragraph separators represent editor line breaks.
    const auto text = [](ElaPlainTextEdit* editor) {
        auto value = editor->document()->toRawText();
        value.replace(QChar::ParagraphSeparator, '\n'); value.replace("\r\n", "\n"); value.replace('\r', '\n');
        return value;
    };
    return {text(m_description), text(m_preview), m_revision};
}
void VoiceDesignerPage::invalidate() {
    ++m_revision; m_state = State::Ready; m_error.clear(); m_timer.stop();
    m_service->cancel(); refresh();
}
void VoiceDesignerPage::generate() {
    if (!m_settings || m_service->isBusy()) return;
    ++m_revision; m_state = State::Running; m_error.clear(); m_elapsed.start(); refresh();
    if (m_service->generate(m_settings->aiConfiguration(), input())) m_timer.start();
    refresh();
}
void VoiceDesignerPage::cancel() {
    if (!m_service->isBusy()) return;
    ++m_revision; m_state = State::Cancelled; m_timer.stop(); m_service->cancel(); refresh();
}
void VoiceDesignerPage::onDeactivated() { cancel(); }
void VoiceDesignerPage::refresh() {
    const auto request = input();
    const auto configuration = m_settings ? m_settings->aiConfiguration() : ai::Configuration{};
    const auto validation = speech::MiMoTtsService::validate(configuration, request);
    const bool busy = m_service->isBusy();
    m_generate->setEnabled(m_settings && !busy && validation.isEmpty());
    m_cancel->setEnabled(busy);
    m_clear->setEnabled(busy || !request.voiceDescription.isEmpty() || !request.previewText.isEmpty() || m_result.has_value());
    m_generate->setToolTip(validation);
    // Show the host only: the URL is configuration, never a source of rich text.
    const QUrl url(configuration.baseUrl, QUrl::StrictMode);
    m_connection->setText(tr("API host: %1 · Model: %2\nUses the saved API address and key in Settings; the text model selection is ignored.")
        .arg(url.host().isEmpty() ? tr("Not configured") : url.host(), speech::MiMoTtsService::modelId()));
    ui::setTone(m_status, m_state == State::Error ? ui::Tone::Danger : m_state == State::Success ? ui::Tone::Success : ui::Tone::Secondary);
    if (m_state == State::Running) m_status->setText(tr("Generating preview… %1 s elapsed. You can cancel; editing either text also cancels this request.").arg(m_elapsed.elapsed() / 1000));
    else if (m_state == State::Error) m_status->setText(m_error);
    else if (m_state == State::Cancelled) m_status->setText(tr("Generation cancelled."));
    else if (m_state == State::Success && resultIsCurrent()) m_status->setText(tr("WAV received and validated (%1 KiB). Playback and saving will be added in the next stage.").arg((m_result->audioData.size() + 1023) / 1024));
    else if (m_result && !resultIsCurrent()) m_status->setText(tr("Inputs or API settings changed. The previous audio is out of date; generate a new preview."));
    else if (request.voiceDescription.trimmed().isEmpty() || request.previewText.trimmed().isEmpty()) m_status->setText(tr("Enter a voice description and the exact text to read aloud."));
    else if (!validation.isEmpty()) m_status->setText(validation);
    else m_status->setText(tr("Ready. Only Generate preview sends these two texts to the configured API."));
}
void VoiceDesignerPage::retranslate() {
    m_title->setText(tr("Voice Designer"));
    m_hint->setText(tr("Describe a voice, then generate a reading of your preview text. This uses an online API and may incur charges. Repeating a description does not guarantee the same voice."));
    m_descriptionLabel->setText(tr("Voice description")); m_previewLabel->setText(tr("Preview text"));
    m_description->setPlaceholderText(tr("For example: a warm, low adult voice, calm and clear, with an unhurried pace."));
    m_preview->setPlaceholderText(tr("Enter the text you want this voice to read. It will be submitted without automatic rewriting."));
    m_generate->setText(tr("Generate preview")); m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear"));
}
void VoiceDesignerPage::changeEvent(QEvent* event) {
    ToolPage::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) { retranslate(); refresh(); }
}
void VoiceDesignerPage::resizeEvent(QResizeEvent* event) {
    ToolPage::resizeEvent(event);
    m_editors->setDirection(width() >= 760 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
}
