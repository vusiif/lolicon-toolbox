#include "FilenameRecoveryPage.h"
#include "UiTheme.h"
#include <ElaScrollArea.h>
#include <ElaPushButton.h>
#include <ElaTreeView.h>
#include <ElaLineEdit.h>
#include <ElaPlainTextEdit.h>
#include <ElaTheme.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QtConcurrentRun>
using namespace mojibake;
FilenameRecoveryPage::FilenameRecoveryPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content = new QWidget(scroll); content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false); scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(32, 28, 32, 28); layout->setSpacing(12);
    m_title = ui::label({}, 18, ui::Tone::Primary, content);
    m_description = ui::label({}, 10, ui::Tone::Secondary, content);
    m_path = ui::label({}, 10, ui::Tone::Secondary, content); m_path->setTextFormat(Qt::PlainText);
    m_status = ui::label({}, 10, ui::Tone::Secondary, content); m_status->setTextFormat(Qt::PlainText); m_status->setObjectName("filenameStatus");
    auto button = [content](const char* name) { auto* b = new ElaPushButton(content); b->setObjectName(name); return b; };
    m_open = button("filenameOpen"); ui::primaryButton(m_open);
    m_rescan = button("filenameRescan"); m_cancel = button("filenameCancel"); m_export = button("filenameExport");
    auto* actions = new QHBoxLayout; actions->addWidget(m_open); actions->addWidget(m_rescan); actions->addWidget(m_cancel); actions->addStretch();
    m_tree = new ElaTreeView(content); m_tree->setObjectName("filenameTree"); m_tree->setMinimumHeight(260);
    m_tree->setAnimated(false); m_tree->setIndentation(24); m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection); m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers); m_tree->setExpandsOnDoubleClick(true);
    m_model = new QStandardItemModel(this); m_tree->setModel(m_model);
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive); m_tree->header()->setStretchLastSection(true);
    auto* candidatesRow = new QHBoxLayout; candidatesRow->setSpacing(8);
    for (int i = 0; i < 5; ++i) {
        auto* b = button(qPrintable(QString("filenameCandidate%1").arg(i + 1)));
        b->setCheckable(true); b->setFixedHeight(60); b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto font = b->font(); font.setPixelSize(12); b->setFont(font); m_candidates[i] = b; candidatesRow->addWidget(b, 1);
        connect(b, &ElaPushButton::clicked, this, [this, i] {
            if (m_selected >= 0 && i < m_plan.entries[m_selected].candidates.size()) setName(m_plan.entries[m_selected].candidates[i].text);
        });
    }
    m_nameLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_name = new ElaLineEdit(content); m_name->setObjectName("filenameProposedName");
    m_apply = button("filenameApplyName"); m_original = button("filenameOriginalName");
    auto* nameRow = new QHBoxLayout; nameRow->addWidget(m_nameLabel); nameRow->addWidget(m_name, 1); nameRow->addWidget(m_apply); nameRow->addWidget(m_original);
    m_details = new ElaPlainTextEdit(content); m_details->setReadOnly(true); m_details->setMinimumHeight(130); m_details->setObjectName("filenameDetails");
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addLayout(actions); layout->addWidget(m_path);
    layout->addWidget(m_status); layout->addWidget(m_tree); layout->addLayout(candidatesRow); layout->addLayout(nameRow);
    layout->addWidget(m_details); layout->addWidget(m_export, 0, Qt::AlignLeft); layout->addStretch();
    connect(m_open, &ElaPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Choose the source directory"));
        if (!path.isEmpty()) scanDirectory(path);
    });
    connect(m_rescan, &ElaPushButton::clicked, this, [this] { scanDirectory(m_directory); });
    connect(m_cancel, &ElaPushButton::clicked, this, [this] { if (m_cancelToken) m_cancelToken->store(true); });
    connect(m_apply, &ElaPushButton::clicked, this, [this] { setName(m_name->text()); });
    connect(m_name, &QLineEdit::returnPressed, this, [this] { setName(m_name->text()); });
    connect(m_original, &ElaPushButton::clicked, this, [this] {
        if (m_selected >= 0) setName(m_plan.entries[m_selected].relativePath.section('/', -1));
    });
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        selectEntry(index.isValid() ? index.siblingAtColumn(0).data(Qt::UserRole).toInt() : -1);
    });
    connect(m_export, &ElaPushButton::clicked, this, [this] {
        const QFileInfo source(m_plan.root);
        QFileDialog dialog(this, tr("Create a directory copy with these names"), source.dir().filePath(source.fileName() + ".repaired"));
        dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setFileMode(QFileDialog::AnyFile); dialog.setOption(QFileDialog::DontConfirmOverwrite);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) exportCopy(dialog.selectedFiles().first());
    });
    connect(&m_scan, &QFutureWatcher<NamePlan>::finished, this, [this] { m_plan = m_scan.result(); m_timer.stop(); rebuildTree(); refresh(); });
    connect(&m_copy, &QFutureWatcher<NameCopyResult>::finished, this, [this] { m_copyResult = m_copy.result(); m_timer.stop(); refresh(); });
    m_timer.setInterval(250); connect(&m_timer, &QTimer::timeout, this, [this] { refresh(); });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { refresh(); });
    retranslate();
}
FilenameRecoveryPage::~FilenameRecoveryPage() { if (m_cancelToken) m_cancelToken->store(true); }
bool FilenameRecoveryPage::busy() const { return m_scan.isRunning() || m_copy.isRunning(); }
void FilenameRecoveryPage::scanDirectory(const QString& path) {
    if (path.isEmpty() || busy()) return;
    m_directory = path; m_plan = {}; m_copyResult = {}; m_selected = -1; m_model->removeRows(0, m_model->rowCount()); m_rows.clear();
    m_cancelToken = std::make_shared<std::atomic_bool>(false); m_progress = std::make_shared<NameProgress>();
    const auto cancel = m_cancelToken; const auto progress = m_progress;
    m_scan.setFuture(QtConcurrent::run([path, cancel, progress] { return FilenameRecovery::scan(path, cancel, progress); }));
    m_timer.start(); refresh();
}
void FilenameRecoveryPage::exportCopy(const QString& path) {
    if (path.isEmpty() || busy() || !m_plan.error.isEmpty() || m_plan.cancelled || m_plan.root.isEmpty() || !FilenameRecovery::validate(m_plan).valid()) return;
    m_copyResult = {}; m_cancelToken = std::make_shared<std::atomic_bool>(false); m_progress = std::make_shared<NameProgress>();
    const auto plan = m_plan; const auto cancel = m_cancelToken; const auto progress = m_progress;
    m_copy.setFuture(QtConcurrent::run([plan, path, cancel, progress] { return FilenameRecovery::copy(plan, path, cancel, progress); }));
    m_timer.start(); refresh();
}
void FilenameRecoveryPage::rebuildTree() {
    m_model->removeRows(0, m_model->rowCount()); m_rows.clear(); m_selected = -1;
    if (!m_plan.error.isEmpty() || m_plan.cancelled) return;
    for (int i = 0; i < m_plan.entries.size(); ++i) {
        const auto& e = m_plan.entries[i];
        auto* item = new QStandardItem(e.relativePath.section('/', -1)); item->setData(i, Qt::UserRole);
        auto font = item->font(); font.setBold(e.directory); item->setFont(font);
        QList<QStandardItem*> row{item, new QStandardItem(e.selectedName), new QStandardItem(), new QStandardItem(e.directory ? tr("Folder") : tr("File"))};
        if (e.parent >= 0) m_rows[e.parent]->appendRow(row); else m_model->appendRow(row);
        m_rows.append(item);
    }
    m_tree->expandToDepth(1); m_tree->setColumnWidth(0, 210); m_tree->setColumnWidth(1, 210); m_tree->setColumnWidth(2, 180);
    refreshPlan();
    if (!m_rows.isEmpty()) m_tree->setCurrentIndex(m_rows.first()->index());
}
void FilenameRecoveryPage::refreshPlan() {
    const auto validation = FilenameRecovery::validate(m_plan);
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& e = m_plan.entries[i]; auto* parent = m_rows[i]->parent();
        auto* name = parent ? parent->child(m_rows[i]->row(), 1) : m_model->item(m_rows[i]->row(), 1);
        auto* status = parent ? parent->child(m_rows[i]->row(), 2) : m_model->item(m_rows[i]->row(), 2);
        name->setText(e.selectedName); name->setToolTip(validation.paths[i]);
        status->setText(!validation.errors[i].isEmpty() ? validation.errors[i] : e.selectedName != e.relativePath.section('/', -1) ? tr("Name will change") : tr("Keep original"));
        status->setToolTip(status->text());
        m_rows[i]->setToolTip(e.relativePath);
    }
}
void FilenameRecoveryPage::setName(const QString& name) {
    if (busy() || m_selected < 0) return;
    m_plan.entries[m_selected].selectedName = name; m_name->setText(name); m_copyResult = {}; refreshPlan(); refresh();
}
void FilenameRecoveryPage::selectEntry(int index) {
    m_selected = index;
    if (index >= 0 && index < m_plan.entries.size()) m_name->setText(m_plan.entries[index].selectedName);
    refresh();
}
void FilenameRecoveryPage::refresh() {
    const bool working = busy(); const auto validation = FilenameRecovery::validate(m_plan);
    m_open->setEnabled(!working); m_rescan->setEnabled(!working && !m_directory.isEmpty()); m_tree->setEnabled(!working);
    m_cancel->setVisible(working); m_export->setEnabled(!working && !m_plan.root.isEmpty() && m_plan.error.isEmpty() && !m_plan.cancelled && validation.valid());
    const bool selected = m_selected >= 0 && m_selected < m_plan.entries.size();
    m_name->setEnabled(selected && !working); m_apply->setEnabled(selected && !working); m_original->setEnabled(selected && !working);
    m_path->setText(m_directory.isEmpty() ? tr("No directory selected.") : m_directory);
    if (m_scan.isRunning()) m_status->setText(tr("Scanning names and hashing file bytes locally: %1 entries, %2 MiB…").arg(m_progress->entries.load()).arg(double(m_progress->bytes.load()) / (1024 * 1024), 0, 'f', 1));
    else if (m_copy.isRunning()) m_status->setText(tr("Verifying and copying: %1 / %2 entries, %3 MiB written…").arg(m_progress->entries.load()).arg(m_plan.entries.size()).arg(double(m_progress->bytes.load()) / (1024 * 1024), 0, 'f', 1));
    else if (!m_copyResult.error.isEmpty()) m_status->setText(m_copyResult.error + (m_copyResult.outputCreated ? tr(" Partial output remains at %1; it is not a completed recovery. Choose a new destination for another attempt.").arg(m_copyResult.destination) : QString()));
    else if (m_copyResult.success) m_status->setText(tr("Created directory copy: %1. Source names and file bytes were preserved.").arg(m_copyResult.destination));
    else if (!m_plan.error.isEmpty()) m_status->setText(m_plan.error);
    else if (m_plan.cancelled) m_status->setText(tr("Scan cancelled; no complete plan is available."));
    else if (!m_plan.root.isEmpty()) {
        int changes = 0, errors = 0;
        for (int i = 0; i < m_plan.entries.size(); ++i) { changes += m_plan.entries[i].selectedName != m_plan.entries[i].relativePath.section('/', -1); errors += !validation.errors[i].isEmpty(); }
        m_status->setText(tr("%1 entries, %2 MiB; %3 name changes, %4 errors. Review the plan before creating a copy.").arg(m_plan.entries.size()).arg(double(m_plan.totalBytes) / (1024 * 1024), 0, 'f', 1).arg(changes).arg(errors));
    } else m_status->setText(tr("Choose a small directory. All entries will be copied; only names selected in the plan change."));
    if (!selected) { m_name->clear(); m_details->clear(); }
    const NameEntry* entry = selected ? &m_plan.entries[m_selected] : nullptr;
    for (int i = 0; i < 5; ++i) {
        const bool available = entry && i < entry->candidates.size(); auto* b = m_candidates[i];
        b->setEnabled(available && !working); b->setChecked(available && entry->selectedName == entry->candidates[i].text);
        b->setText(available ? QString("%1\n%2 / 100").arg(b->fontMetrics().elidedText(entry->candidates[i].text, Qt::ElideMiddle, std::max(60, b->width() - 16))).arg(entry->candidates[i].score, 0, 'f', 1) : tr("Candidate %1\nUnavailable").arg(i + 1));
        b->setToolTip(available ? entry->candidates[i].text : QString());
        for (auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
            const bool chosen = b->isChecked();
            const auto base = eTheme->getThemeColor(mode, chosen ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase);
            const auto hover = eTheme->getThemeColor(mode, chosen ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover);
            const auto press = eTheme->getThemeColor(mode, chosen ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress);
            const auto color = eTheme->getThemeColor(mode, chosen ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText);
            if (mode == ElaThemeType::Light) { b->setLightDefaultColor(base); b->setLightHoverColor(hover); b->setLightPressColor(press); b->setLightTextColor(color); }
            else { b->setDarkDefaultColor(base); b->setDarkHoverColor(hover); b->setDarkPressColor(press); b->setDarkTextColor(color); }
        }
        b->update();
    }
    if (entry) {
        // Do not overwrite an in-progress manual edit on every timer tick.
        if (!m_name->hasFocus() || working) m_name->setText(entry->selectedName);
        QStringList details{tr("Original relative path: %1").arg(entry->relativePath), tr("Planned relative path: %1").arg(validation.paths[m_selected])};
        if (!validation.errors[m_selected].isEmpty()) details << validation.errors[m_selected];
        for (int i = 0; i < entry->candidates.size(); ++i) {
            const auto& c = entry->candidates[i];
            details << tr("Candidate %1: %2 — %3").arg(i + 1).arg(c.text, c.reversible ? tr("Exact inverse verified") : tr("Partial recovery; not reversible"));
            if (c.text == entry->selectedName) for (const auto& step : c.path)
                details << tr("Encode as %1, then decode as %2.").arg(EncodingConverter::name(step.encodeAs), EncodingConverter::name(step.decodeAs));
        }
        if (entry->relativePath.contains(QChar(0xfffd)) || entry->selectedName.contains(QChar(0xfffd)))
            details << tr("Possible missing information: replacement characters do not identify the original name.");
        details << tr("Scores rank possibilities, not probabilities. Extensions are preserved in generated file-name candidates. No names or contents are sent to AI.");
        m_details->setPlainText(details.join('\n'));
    }
}
void FilenameRecoveryPage::retranslate() {
    m_title->setText(tr("File and folder name recovery"));
    m_description->setText(tr("Scan a directory, select name candidates and review the complete plan. Create a new directory copy; file contents stay unchanged. Limits: 1,000 entries, 32 levels, 256 MiB. Links are not followed."));
    m_open->setText(tr("Choose directory")); m_rescan->setText(tr("Scan again")); m_cancel->setText(tr("Cancel"));
    m_export->setText(tr("Create directory copy…")); m_nameLabel->setText(tr("Proposed name")); m_apply->setText(tr("Apply to plan")); m_original->setText(tr("Keep original"));
    m_model->setHorizontalHeaderLabels({tr("Original name"), tr("Proposed name"), tr("Plan status"), tr("Type")});
    if (!m_rows.isEmpty()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            auto* parent = m_rows[i]->parent(); auto* type = parent ? parent->child(m_rows[i]->row(), 3) : m_model->item(m_rows[i]->row(), 3);
            type->setText(m_plan.entries[i].directory ? tr("Folder") : tr("File"));
        }
        refreshPlan();
    }
    refresh();
}
void FilenameRecoveryPage::changeEvent(QEvent* event) { QWidget::changeEvent(event); if (event->type() == QEvent::LanguageChange) retranslate(); }
