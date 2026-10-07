#pragma once
#include "tool/ToolPage.h"
#include "MiMoTtsService.h"
#include <QElapsedTimer>
#include <QTimer>
class AppSettings;
class QLabel;
class ElaPlainTextEdit;
class ElaPushButton;
class QBoxLayout;

class VoiceDesignerPage : public ToolPage {
    Q_OBJECT
public:
    explicit VoiceDesignerPage(QWidget* parent = nullptr, AppSettings* settings = nullptr);
    void onDeactivated() override;
    // Current-session snapshot only; no settings/history serialization.
    const std::optional<speech::VoiceDesignResult>& result() const { return m_result; }
    bool resultIsCurrent() const { return m_result && m_result->snapshot.revision == m_revision; }
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
    speech::VoiceDesignRequest input() const;
    AppSettings* m_settings;
    speech::MiMoTtsService* m_service;
    std::optional<speech::VoiceDesignResult> m_result;
    quint64 m_revision = 0;
    State m_state = State::Ready;
    QString m_error;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    QLabel *m_title, *m_hint, *m_descriptionLabel, *m_previewLabel, *m_status, *m_connection;
    ElaPlainTextEdit *m_description, *m_preview;
    ElaPushButton *m_generate, *m_cancel, *m_clear;
    QBoxLayout* m_editors;
};
