#pragma once
#include <QWidget>
#include <QFutureWatcher>
#include <array>
#include "RecoveryOperation.h"
class ElaPushButton;
class ElaPlainTextEdit;
class ElaComboBox;
class ElaCheckBox;
class QLabel;
class QButtonGroup;
class TextFileRecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit TextFileRecoveryPage(QWidget* parent = nullptr);
    ~TextFileRecoveryPage() override;
    void openFile(const QString& path);
    void exportCopy(const QString& destination);
protected:
    void changeEvent(QEvent* event) override;
private:
    void retranslate();
    void refresh();
    void showCandidate();
    std::optional<mojibake::Encoding> forcedEncoding() const;
    mojibake::Encoding outputEncoding() const;
    QLabel *m_title, *m_description, *m_pathLabel, *m_status, *m_sourceLabel, *m_outputLabel, *m_newlineLabel;
    ElaPushButton *m_open, *m_analyze, *m_cancel, *m_save;
    ElaComboBox *m_source, *m_output, *m_newlines;
    ElaCheckBox* m_bom;
    ElaPlainTextEdit *m_preview, *m_details;
    std::array<ElaPushButton*, 5> m_buttons;
    QButtonGroup* m_candidates;
    QFutureWatcher<mojibake::FileResult> m_analysis;
    QFutureWatcher<mojibake::SaveResult> m_export;
    mojibake::Cancellation m_cancelToken;
    mojibake::FileResult m_result;
    QString m_path, m_savedPath, m_pendingDestination, m_saveError;
    int m_selected = -1;
    bool m_resetOutput = true;
};
