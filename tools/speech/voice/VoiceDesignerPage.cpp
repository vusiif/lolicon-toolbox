#include "VoiceDesignerPage.h"
#include "services/AppSettings.h"
#include "UiTheme.h"
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaSlider.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QAudioOutput>
#include <QBoxLayout>
#include <QBuffer>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMediaPlayer>
#include <climits>
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
    m_play = button("voicePlay"); m_stop = button("voiceStop"); m_save = button("voiceSave");
    m_position = new ElaSlider(Qt::Horizontal, content); m_position->setObjectName("voicePosition");
    m_position->setRange(0, 0); m_position->setMinimumWidth(80);
    m_time = ui::label({}, 10, ui::Tone::Secondary, content); m_time->setObjectName("voiceTime");
    m_noticeLabel = ui::label({}, 10, ui::Tone::Secondary, content); m_noticeLabel->setWordWrap(true);
    m_noticeLabel->setTextFormat(Qt::PlainText); m_noticeLabel->setObjectName("voiceNotice");
    auto* playback = new QHBoxLayout; playback->addWidget(m_play); playback->addWidget(m_stop);
    playback->addWidget(m_position, 1); playback->addWidget(m_time); playback->addWidget(m_save);
    layout->addWidget(m_title); layout->addWidget(m_hint); layout->addWidget(m_connection);
    layout->addLayout(m_editors); layout->addLayout(actions); layout->addWidget(m_status);
    layout->addLayout(playback); layout->addWidget(m_noticeLabel); layout->addStretch();
    connect(m_play, &ElaPushButton::clicked, this, &VoiceDesignerPage::togglePlayback);
    connect(m_stop, &ElaPushButton::clicked, this, [this] { if (m_player) m_player->stop(); refresh(); });
    connect(m_position, &QSlider::sliderMoved, this, [this](const int value) { if (m_player && m_sourceLoaded) m_player->setPosition(value); });
    connect(m_save, &ElaPushButton::clicked, this, [this] {
        if (!resultIsCurrent()) return;
        if (const auto path = QFileDialog::getSaveFileName(this, tr("Save WAV"), "voice-preview.wav", tr("WAV audio (*.wav)"), nullptr, QFileDialog::DontConfirmOverwrite);
            !path.isEmpty()) saveWav(path);
    });
    connect(m_description, &ElaPlainTextEdit::textChanged, this, &VoiceDesignerPage::invalidate);
    connect(m_preview, &ElaPlainTextEdit::textChanged, this, &VoiceDesignerPage::invalidate);
    connect(m_generate, &ElaPushButton::clicked, this, &VoiceDesignerPage::generate);
    connect(m_cancel, &ElaPushButton::clicked, this, &VoiceDesignerPage::cancel);
    connect(m_clear, &ElaPushButton::clicked, this, [this] {
        const QSignalBlocker blockDescription(m_description), blockPreview(m_preview);
        m_description->clear(); m_preview->clear(); m_result.reset(); m_wav.reset(); invalidate();
    });
    if (settings) {
        connect(settings, &AppSettings::voiceConfigurationChanged, this, &VoiceDesignerPage::invalidate);
        // An unsaved voice API still borrows the text API address and key.
        connect(settings, &AppSettings::aiConfigurationChanged, this, [this] { if (!m_settings->hasVoiceConfiguration()) invalidate(); });
    }
    connect(m_service, &speech::MiMoTtsService::completed, this, [this](const speech::VoiceDesignResult& result) {
        if (result.snapshot.revision != m_revision) return;
        unloadPlayback();
        m_result = result; m_wav = speech::MiMoTtsService::wavInfo(result.audioData);
        m_state = State::Success; m_timer.stop(); refresh();
    });
    connect(m_service, &speech::MiMoTtsService::failed, this, [this](speech::VoiceDesignError, const QString& error, const quint64 revision) {
        if (revision != m_revision) return;
        m_error = error; m_state = State::Error; m_timer.stop(); refresh();
    });
    connect(m_service, &speech::MiMoTtsService::cancelled, this, [this](const quint64 revision) {
        if (revision != m_revision) return;
        m_state = State::Cancelled; m_timer.stop(); refresh();
    });
    m_timer.setInterval(500); connect(&m_timer, &QTimer::timeout, this, &VoiceDesignerPage::refresh);
    retranslate(); refresh();
}
speech::VoiceDesignRequest VoiceDesignerPage::input() const {
    // toRawText retains NBSP; paragraph separators represent editor line breaks.
    const auto text = [](const ElaPlainTextEdit* editor) {
        auto value = editor->document()->toRawText();
        value.replace(QChar::ParagraphSeparator, '\n'); value.replace("\r\n", "\n"); value.replace('\r', '\n');
        return value;
    };
    return {.voiceDescription = text(m_description), .previewText = text(m_preview), .revision = m_revision};
}
VoiceDesignerPage::~VoiceDesignerPage() {
    // Release the buffer while the widgets still exist, without refreshing a half-destroyed page.
    if (m_player) { static_cast<void>(m_player->disconnect(this)); unloadPlayback(); }
}
void VoiceDesignerPage::invalidate() {
    ++m_revision; m_state = State::Ready; m_error.clear(); m_timer.stop();
    unloadPlayback(); setNotice({}, false); m_service->cancel(); refresh();
}
void VoiceDesignerPage::generate() {
    if (!m_settings || m_service->isBusy()) return;
    unloadPlayback(); setNotice({}, false);
    ++m_revision; m_state = State::Running; m_error.clear(); m_elapsed.start(); refresh();
    if (m_service->generate(m_settings->voiceConfiguration(), input())) m_timer.start();
    refresh();
}
void VoiceDesignerPage::cancel() {
    if (!m_service->isBusy()) return;
    ++m_revision; m_state = State::Cancelled; m_timer.stop(); m_service->cancel(); refresh();
}
void VoiceDesignerPage::onDeactivated() {
    if (m_player) m_player->pause();
    cancel();
}
namespace {
QString formatClock(const qint64 ms) {
    const auto seconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}
bool VoiceDesignerPage::isPlaying() const { return m_player && m_player->playbackState() == QMediaPlayer::PlayingState; }
void VoiceDesignerPage::ensurePlayer() {
    if (m_player) return;
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(m_player);
    m_player->setAudioOutput(m_audioOutput);
    m_buffer = new QBuffer(m_player); // Owned by the player so it can never outlive or predecease it.
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &VoiceDesignerPage::refresh);
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](const qint64 position) {
        if (!m_position->isSliderDown()) { const QSignalBlocker blocker(m_position); m_position->setValue(static_cast<int>(position)); }
        refresh();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](const QMediaPlayer::Error error) {
        if (error == QMediaPlayer::NoError) return;
        // Backend messages may be untranslated or technical; the WAV itself is already validated.
        setNotice(tr("This system could not play the audio. You can still save the WAV and open it in another player."), true);
        refresh();
    });
}
void VoiceDesignerPage::unloadPlayback() {
    if (!m_player) return;
    m_player->stop();
    m_player->setSourceDevice(nullptr);
    m_buffer->close(); m_buffer->setData(QByteArray());
    m_sourceLoaded = false;
    const QSignalBlocker blocker(m_position); m_position->setValue(0);
}
void VoiceDesignerPage::togglePlayback() {
    if (!resultIsCurrent()) return;
    ensurePlayer();
    if (isPlaying()) { m_player->pause(); return; }
    if (!m_sourceLoaded) {
        m_buffer->setData(m_result->audioData);
        if (!m_buffer->open(QIODevice::ReadOnly)) { setNotice(tr("This system could not play the audio. You can still save the WAV and open it in another player."), true); refresh(); return; }
        // The URL is only a format hint for the backend; nothing is read from disk.
        m_player->setSourceDevice(m_buffer, QUrl(QStringLiteral("voice-preview.wav")));
        m_sourceLoaded = true;
    }
    setNotice({}, false);
    m_player->play();
}
void VoiceDesignerPage::setNotice(const QString& text, const bool error) { m_notice = text; m_noticeError = error; }
bool VoiceDesignerPage::saveWav(const QString& path) {
    if (!resultIsCurrent()) return false;
    QFile file(path);
    const auto& audio = m_result->audioData;
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setNotice(tr("Could not create the file. Choose a new filename; existing files are never overwritten."), true);
    } else if (file.write(audio) != audio.size() || !file.flush()) {
        file.close(); file.remove(); // Do not leave a truncated WAV behind.
        setNotice(tr("Could not write the WAV file. Check free space and permissions."), true);
    } else {
        file.close();
        setNotice(tr("Saved to %1").arg(QDir::toNativeSeparators(path)), false);
        refresh(); return true;
    }
    refresh(); return false;
}
void VoiceDesignerPage::refresh() {
    const auto request = input();
    const auto configuration = m_settings ? m_settings->voiceConfiguration() : ai::Configuration{};
    const auto validation = speech::MiMoTtsService::validate(configuration, request);
    const bool busy = m_service->isBusy();
    m_generate->setEnabled(m_settings && !busy && validation.isEmpty());
    m_cancel->setEnabled(busy);
    m_clear->setEnabled(busy || !request.voiceDescription.isEmpty() || !request.previewText.isEmpty() || m_result.has_value());
    m_generate->setToolTip(validation);
    // Show the host only: the URL is configuration, never a source of rich text.
    const QUrl url(configuration.baseUrl, QUrl::StrictMode);
    m_connection->setText((m_settings && !m_settings->hasVoiceConfiguration()
        ? tr("API host: %1 · Model: %2\nThe voice API is not saved in Settings yet, so the text API address and key are used.")
        : tr("API host: %1 · Model: %2\nUses the voice API address, key and model saved in Settings."))
        .arg(url.host().isEmpty() ? tr("Not configured") : url.host(), configuration.model.trimmed().isEmpty() ? tr("Not configured") : configuration.model.trimmed()));
    ui::setTone(m_status, m_state == State::Error ? ui::Tone::Danger : m_state == State::Success ? ui::Tone::Success : ui::Tone::Secondary);
    if (m_state == State::Running) m_status->setText(tr("Generating preview… %1 s elapsed. You can cancel; editing either text also cancels this request.").arg(m_elapsed.elapsed() / 1000));
    else if (m_state == State::Error) m_status->setText(m_error);
    else if (m_state == State::Cancelled) m_status->setText(tr("Generation cancelled."));
    else if (m_state == State::Success && resultIsCurrent()) m_status->setText(tr("WAV received and validated (%1, %2 Hz, %3 KiB). Play it here or save it as a WAV file.")
        .arg(formatClock(m_wav ? m_wav->durationMs() : 0)).arg(m_wav ? m_wav->sampleRate : 0).arg((m_result->audioData.size() + 1023) / 1024));
    else if (m_result && !resultIsCurrent()) m_status->setText(tr("Inputs or API settings changed. The previous audio is out of date; generate a new preview."));
    else if (request.voiceDescription.trimmed().isEmpty() || request.previewText.trimmed().isEmpty()) m_status->setText(tr("Enter a voice description and the exact text to read aloud."));
    else if (!validation.isEmpty()) m_status->setText(validation);
    else m_status->setText(tr("Ready. Only Generate preview sends these two texts to the configured API."));
    // Only the current result is playable or savable; stale audio stays visible as "out of date" only.
    const bool audio = resultIsCurrent() && m_wav.has_value();
    const bool playing = isPlaying();
    const auto duration = audio ? m_wav->durationMs() : 0;
    m_play->setEnabled(audio); m_save->setEnabled(audio); m_position->setEnabled(audio && m_sourceLoaded);
    m_stop->setEnabled(audio && m_player && m_player->playbackState() != QMediaPlayer::StoppedState);
    m_play->setText(playing ? tr("Pause") : tr("Play"));
    if (m_position->maximum() != duration) { const QSignalBlocker blocker(m_position); m_position->setRange(0, static_cast<int>(qMin<qint64>(duration, INT_MAX))); }
    const auto position = audio && m_player && m_sourceLoaded ? m_player->position() : 0;
    m_time->setText(QStringLiteral("%1 / %2").arg(formatClock(position), formatClock(duration)));
    m_noticeLabel->setVisible(!m_notice.isEmpty()); m_noticeLabel->setText(m_notice);
    ui::setTone(m_noticeLabel, m_noticeError ? ui::Tone::Danger : ui::Tone::Success);
}
void VoiceDesignerPage::retranslate() {
    m_title->setText(tr("Voice Designer"));
    m_hint->setText(tr("Describe a voice, then generate a reading of your preview text. This uses an online API and may incur charges. Repeating a description does not guarantee the same voice."));
    m_descriptionLabel->setText(tr("Voice description")); m_previewLabel->setText(tr("Preview text"));
    m_description->setPlaceholderText(tr("For example: a warm, low adult voice, calm and clear, with an unhurried pace."));
    m_preview->setPlaceholderText(tr("Enter the text you want this voice to read. It will be submitted without automatic rewriting."));
    m_generate->setText(tr("Generate preview")); m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear"));
    m_stop->setText(tr("Stop")); m_save->setText(tr("Save WAV…"));
}
void VoiceDesignerPage::changeEvent(QEvent* event) {
    ToolPage::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) { retranslate(); refresh(); }
}
void VoiceDesignerPage::resizeEvent(QResizeEvent* event) {
    ToolPage::resizeEvent(event);
    m_editors->setDirection(width() >= 760 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
}
