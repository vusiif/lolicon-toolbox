#pragma once
#include "DiagnosticReport.h"
#include <QWidget>
class QLabel;
class ElaPushButton;
class DiagnosticReportWidget : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticReportWidget(QWidget* parent = nullptr);
    void setReport(mojibake::DiagnosticReport report);
    bool exportReport(const QString& path);
protected:
    void changeEvent(QEvent* event) override;
private:
    void refresh();
    mojibake::DiagnosticReport m_report;
    QLabel *m_summary, *m_notice, *m_status;
    ElaPushButton* m_export;
};
