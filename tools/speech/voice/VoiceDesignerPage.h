#pragma once
#include "tool/ToolPage.h"
#include "MiMoTtsService.h"
#include <QElapsedTimer>
#include <QTimer>
class AppSettings;
class QLabel;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaSlider;
class QBoxLayout;
class QBuffer;
class QMediaPlayer;
class QAudioOutput;

class VoiceDesignerPage : public ToolPage {
    Q_OBJECT
public:
    explicit VoiceDesignerPage(QWidget* parent = nullptr, AppSettings* settings = nullptr);
    ~VoiceDesignerPage() override;
    void onDeactivated() override;
    // Current-session snapshot only; no settings/history serialization.
    const std::optional<speech::VoiceDesignResult>& result() const { return m_result; }
    bool resultIsCurrent() const { return m_result && m_result->snapshot.revision == m_revision; }
    bool isPlaying() const;
    // Writes the current result to a path that must not exist yet; the dialog only chooses it.
    bool saveWav(const QString& path);
protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    enum class State { Ready, Running, Success, Error, Cancelled };
    void invalidate();
    void generate();
    void cancel();
    void refresh();
    void retranslate();
    void togglePlayback();
    void unloadPlayback();
    void ensurePlayer();
    void setNotice(const QString& text, bool error);
    speech::VoiceDesignRequest input() const;
    AppSettings* m_settings;
    speech::MiMoTtsService* m_service;
    std::optional<speech::VoiceDesignResult> m_result;
    std::optional<speech::WavInfo> m_wav;
    quint64 m_revision = 0;
    State m_state = State::Ready;
    QString m_error, m_notice;
    bool m_noticeError = false;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    QLabel *m_title, *m_hint, *m_descriptionLabel, *m_previewLabel, *m_status, *m_connection;
    QLabel *m_time, *m_noticeLabel;
    ElaPlainTextEdit *m_description, *m_preview;
    ElaPushButton *m_generate, *m_cancel, *m_clear;
    ElaPushButton *m_play, *m_stop, *m_save;
    ElaSlider* m_position;
    QBoxLayout* m_editors;
    // Created on first Play so opening the page never initializes a media backend.
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QBuffer* m_buffer = nullptr;
    bool m_sourceLoaded = false;
};
