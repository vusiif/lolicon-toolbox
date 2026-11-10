#pragma once
#include <QWidget>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <memory>
#include "RecoveryOperation.h"
class AppSettings;
class ElaPushButton;
class ElaPlainTextEdit;
class ElaComboBox;
class ElaCheckBox;
class ElaProgressBar;
class QLabel;
class DiagnosticReportWidget;
class RecoveryWorkspace;
class MojibakeAi;
class TextFileRecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit TextFileRecoveryPage(QWidget* parent = nullptr, AppSettings* settings = nullptr);
    ~TextFileRecoveryPage() override;
    void openFile(const QString& path);
    void exportCopy(const QString& destination);
protected:
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
private:
    void retranslate();
    void refresh();
    void refreshCards();
    void showSelection();
    void requestAi();
    void aiChanged();
    void updateAiControls();
    bool lossyTop() const;
    std::optional<mojibake::Encoding> forcedEncoding() const;
    mojibake::Encoding outputEncoding() const;
    QLabel *m_fileLabel, *m_description, *m_pathLabel, *m_status, *m_sourceLabel, *m_resultLabel, *m_outputLabel, *m_newlineLabel;
    QLabel *m_aiHint, *m_aiStatus;
    ElaPushButton *m_open, *m_analyze, *m_cancel, *m_save, *m_detailsToggle, *m_aiAnalyze, *m_aiCancel;
    ElaComboBox *m_source, *m_output, *m_newlines;
    ElaCheckBox* m_bom;
    ElaPlainTextEdit* m_details;
    ElaProgressBar *m_progress, *m_aiProgress;
    QWidget* m_detailsBox;
    RecoveryWorkspace* m_workspace;
    DiagnosticReportWidget* m_report;
    MojibakeAi* m_ai;
    QFutureWatcher<mojibake::FileResult> m_analysis;
    QFutureWatcher<mojibake::SaveResult> m_export;
    mojibake::Cancellation m_cancelToken;
    std::shared_ptr<std::atomic_int> m_analysisProgress;
    QTimer m_progressPoll;
    mojibake::FileResult m_result;
    QString m_path, m_savedPath, m_pendingDestination, m_saveError;
    quint64 m_aiRevision = 0;
    bool m_resetOutput = true;
    AppSettings* m_settings;
};
