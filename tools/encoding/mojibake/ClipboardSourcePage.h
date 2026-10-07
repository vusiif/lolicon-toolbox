#pragma once
#include "ClipboardSource.h"
#include <QWidget>
#include <QFutureWatcher>
class ElaPushButton;
class ElaPlainTextEdit;
class QLabel;
class DiagnosticReportWidget;
class ClipboardSourcePage : public QWidget {
    Q_OBJECT
public:
    explicit ClipboardSourcePage(QWidget* parent = nullptr);
    void inspect(const QMimeData& mime);
signals:
    void textChosen(const QString& text);
protected:
    void changeEvent(QEvent* event) override;
private:
    void retranslate();
    void refresh();
    QLabel *m_title, *m_description, *m_status;
    ElaPushButton *m_read, *m_plain, *m_html, *m_use, *m_clear;
    ElaPlainTextEdit *m_formats, *m_plainPreview, *m_htmlPreview, *m_details;
    DiagnosticReportWidget* m_report;
    QFutureWatcher<mojibake::ClipboardTexts> m_worker;
    mojibake::ClipboardTexts m_snapshot;
    int m_selected = -1;
    bool m_readOnce = false;
};
