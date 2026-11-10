#pragma once
#include "tool/ToolPage.h"
#include "MojibakeCandidate.h"
#include <QFutureWatcher>
#include <QTimer>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
class AppSettings;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaProgressBar;
class QButtonGroup;
class QLabel;
class QMimeData;
class QStackedWidget;
class DiagnosticReportWidget;
class RecoveryWorkspace;
class MojibakeAi;
class TextFileRecoveryPage;
struct WorkspaceCandidate;
class MojibakePage : public ToolPage {
    Q_OBJECT
public:
    explicit MojibakePage(QWidget* parent = nullptr, AppSettings* settings = nullptr);
    ~MojibakePage() override;
    // Same seam as TextStylePage/CharacterArtPage: tests replace the system
    // clipboard, which other applications (clipboard managers, IMEs) may hold.
    void setClipboardWriter(std::function<bool(const QString&)> writer) { m_clipboardWriter = std::move(writer); }
    // Words for a candidate card; shared with the file page.
    static WorkspaceCandidate describe(const mojibake::Candidate& candidate, bool recommended);
    static QStringList processLines(const mojibake::Candidate& candidate, int examined, bool limited);
protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void analyze();
    void retranslate();
    void showResult();
    void showSelection();
    void invalidate();
    void requestAi();
    void aiChanged();
    void updateAiControls();
    void refreshCards();
    void selectMode(int index);
    bool openDroppedFile(const QMimeData* mime);
    QString inputText() const;
    QString selectedText() const;
    QStackedWidget* m_pages;
    TextFileRecoveryPage* m_filePage;
    std::array<ElaPushButton*, 4> m_modes;
    QButtonGroup* m_modeGroup;
    QLabel *m_title, *m_description, *m_inputLabel, *m_inputHint, *m_outputLabel, *m_status, *m_aiHint, *m_aiStatus;
    ElaPlainTextEdit *m_input, *m_process;
    ElaPushButton *m_analyze, *m_clear, *m_cancel, *m_copy, *m_details, *m_aiAnalyze, *m_aiCancel;
    ElaProgressBar *m_progress, *m_aiProgress;
    QWidget* m_detailsBox;
    RecoveryWorkspace* m_workspace;
    DiagnosticReportWidget* m_report;
    MojibakeAi* m_ai;
    QFutureWatcher<mojibake::Result> m_watcher;
    mojibake::Cancellation m_cancellation;
    std::shared_ptr<std::atomic_int> m_analysisProgress;
    QTimer m_progressPoll;
    mojibake::Result m_result;
    QString m_submitted;
    bool m_stale = false, m_copied = false;
    quint64 m_aiRevision = 0;
    std::function<bool(const QString&)> m_clipboardWriter;
    AppSettings* m_settings;
};
