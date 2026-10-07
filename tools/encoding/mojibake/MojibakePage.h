#pragma once
#include "tool/ToolPage.h"
#include "MojibakeCandidate.h"
#include <QFutureWatcher>
#include <array>
#include <QElapsedTimer>
#include <QTimer>
#include "ai/AiProvider.h"
class AppSettings;
namespace ai { class AiService; }
class ElaPlainTextEdit;
class ElaPushButton;
class QButtonGroup;
class QLabel;
class MojibakePage : public ToolPage {
    Q_OBJECT
public:
    explicit MojibakePage(QWidget* parent = nullptr, AppSettings* settings = nullptr);
    ~MojibakePage() override;
protected:
    void changeEvent(QEvent* event) override;
private:
    void analyze();
    void retranslate();
    void showResult();
    void showCandidate();
    void invalidate();
    void analyzeAi();
    void updateAiControls();
    void invalidateAi();
    QString inputText() const;
    ElaPlainTextEdit *m_input, *m_output, *m_process;
    ElaPushButton *m_analyze, *m_copy, *m_clear, *m_cancel, *m_details;
    ElaPushButton *m_textMode, *m_fileMode, *m_nameMode, *m_clipboardMode;
    std::array<ElaPushButton*, 5> m_candidateButtons;
    QButtonGroup* m_candidates;
    int m_selectedCandidate = -1;
    QLabel *m_title, *m_description, *m_inputLabel, *m_outputLabel, *m_status;
    QFutureWatcher<mojibake::Result> m_watcher;
    mojibake::Cancellation m_cancellation;
    mojibake::Result m_result;
    QString m_submitted;
    bool m_stale = false;
    bool m_copied = false;
    AppSettings* m_settings;
    ai::AiService* m_aiService;
    ElaPushButton *m_aiAnalyze, *m_aiCancel, *m_aiCopy;
    ElaPlainTextEdit *m_aiOutput, *m_aiExplanation;
    QLabel *m_aiTitle, *m_aiHint, *m_aiStatus;
    ai::Reply m_aiResult;
    QString m_aiSubmitted;
    bool m_aiStale = false;
    QElapsedTimer m_aiElapsed;
    QTimer m_aiProgressTimer;
    int m_aiCharacters = 0;
    bool m_aiThinking = false;
    void showAiProgress();
};
