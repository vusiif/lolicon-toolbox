#pragma once
#include "tool/ToolPage.h"
#include "MojibakeCandidate.h"
#include <QFutureWatcher>
class ElaPlainTextEdit;
class ElaPushButton;
class ElaComboBox;
class QLabel;
class MojibakePage : public ToolPage {
    Q_OBJECT
public:
    explicit MojibakePage(QWidget* parent = nullptr);
    ~MojibakePage() override;
protected:
    void changeEvent(QEvent* event) override;
private:
    void analyze();
    void retranslate();
    void showResult();
    void showCandidate();
    void invalidate();
    QString inputText() const;
    ElaPlainTextEdit *m_input, *m_output, *m_process;
    ElaPushButton *m_analyze, *m_copy, *m_clear, *m_cancel, *m_details;
    ElaComboBox* m_candidates;
    QLabel *m_title, *m_description, *m_inputLabel, *m_outputLabel, *m_status;
    QFutureWatcher<mojibake::Result> m_watcher;
    mojibake::Cancellation m_cancellation;
    mojibake::Result m_result;
    QString m_submitted;
    bool m_stale = false;
    bool m_copied = false;
};
