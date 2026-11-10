#pragma once
#include "FilenameRecovery.h"
#include <QWidget>
#include <QFutureWatcher>
#include <QTimer>
#include <array>
class ElaPushButton;
class ElaTreeView;
class ElaLineEdit;
class ElaPlainTextEdit;
class QLabel;
class QStandardItemModel;
class QStandardItem;
class DiagnosticReportWidget;
class FilenameRecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit FilenameRecoveryPage(QWidget* parent = nullptr);
    ~FilenameRecoveryPage() override;
    void scanDirectory(const QString& path);
    void exportCopy(const QString& path);
protected:
    void changeEvent(QEvent* event) override;
private:
    void retranslate();
    void refresh();
    void rebuildTree();
    void refreshPlan();
    void selectEntry(int index);
    void setName(const QString& name);
    bool busy() const;
    QLabel *m_title, *m_description, *m_path, *m_status, *m_nameLabel;
    ElaPushButton *m_open, *m_rescan, *m_cancel, *m_export, *m_apply, *m_original;
    ElaTreeView* m_tree;
    QStandardItemModel* m_model;
    ElaLineEdit* m_name;
    ElaPlainTextEdit* m_details;
    DiagnosticReportWidget* m_report;
    std::array<ElaPushButton*, 5> m_candidates;
    QList<QStandardItem*> m_rows;
    mojibake::NamePlan m_plan;
    mojibake::NameCopyResult m_copyResult;
    int m_selected = -1;
    QString m_directory;
    mojibake::Cancellation m_cancelToken;
    mojibake::NameProgressPtr m_progress;
    QFutureWatcher<mojibake::NamePlan> m_scan;
    QFutureWatcher<mojibake::NameCopyResult> m_copy;
    bool m_scanning = false, m_copying = false;  // until the finished handler ran
    QTimer m_timer;
};
