#include "DiagnosticReportWidget.h"
#include "UiTheme.h"
#include <ElaPushButton.h>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QEvent>
DiagnosticReportWidget::DiagnosticReportWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
    m_summary = ui::label({}, 10, ui::Tone::Primary, this); m_summary->setTextFormat(Qt::PlainText);
    m_summary->setObjectName("diagnosticSummary"); m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_notice = ui::label({}, 9, ui::Tone::Secondary, this); m_status = ui::label({}, 9, ui::Tone::Secondary, this); m_status->setTextFormat(Qt::PlainText);
    m_status->setObjectName("diagnosticExportStatus");
    m_export = new ElaPushButton(this); m_export->setObjectName("diagnosticExport");
    layout->addWidget(m_summary); layout->addWidget(m_notice); layout->addWidget(m_export, 0, Qt::AlignLeft); layout->addWidget(m_status);
    connect(m_export, &ElaPushButton::clicked, this, [this] {
        QFileDialog dialog(this, tr("Export diagnostic report"), "toolbox-diagnosis.json", tr("JSON report (*.json)"));
        dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setDefaultSuffix("json"); dialog.setFileMode(QFileDialog::AnyFile); dialog.setOption(QFileDialog::DontConfirmOverwrite);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) exportReport(dialog.selectedFiles().first());
    });
    refresh();
}
void DiagnosticReportWidget::setReport(mojibake::DiagnosticReport report) {
    if (m_report.data != report.data || m_report.ready != report.ready) m_status->clear();
    m_report = std::move(report); refresh();
}
bool DiagnosticReportWidget::exportReport(const QString& path) {
    QString error; const bool success = m_report.save(path, error);
    m_status->setText(success ? tr("Report saved: %1").arg(path) : error); m_status->setVisible(true); return success;
}
void DiagnosticReportWidget::refresh() {
    m_summary->setText(m_report.summary()); m_export->setText(tr("Export diagnostic report…")); m_export->setEnabled(m_report.ready);
    m_notice->setText(tr("JSON contains evidence, scores and paths/names when applicable; no text body, AI reply or API settings. Export is local and does not run analysis."));
    m_notice->setVisible(m_report.ready); m_status->setVisible(!m_status->text().isEmpty());
}
void DiagnosticReportWidget::changeEvent(QEvent* event) { QWidget::changeEvent(event); if (event->type() == QEvent::LanguageChange) refresh(); }
