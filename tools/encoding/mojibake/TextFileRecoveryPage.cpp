#include "TextFileRecoveryPage.h"
#include "DiagnosticReportWidget.h"
#include "LossyGbkRecovery.h"
#include "MojibakeAi.h"
#include "MojibakePage.h"
#include "RecoveryWorkspace.h"
#include "UiTheme.h"
#include "services/AppSettings.h"
#include <ElaScrollArea.h>
#include <ElaPushButton.h>
#include <ElaPlainTextEdit.h>
#include <ElaComboBox.h>
#include <ElaCheckBox.h>
#include <ElaProgressBar.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSignalBlocker>
#include <QtConcurrentRun>
using namespace mojibake;
namespace {
constexpr qsizetype PreviewCharacters = 32768;
constexpr qsizetype MaxSuggestionBytes = 16 * 1024;
QFrame* card(QWidget* parent) { auto* frame = new QFrame(parent); ui::styleCard(frame); return frame; }
bool lossy(const FileCandidate& c) { return !c.repairPath.isEmpty() && c.repairPath.last().lossy && c.lossy; }
QString singleFile(const QMimeData* mime) {
    if (!mime || !mime->hasUrls() || mime->urls().size() != 1 || !mime->urls().first().isLocalFile()) return {};
    return mime->urls().first().toLocalFile();
}
}
TextFileRecoveryPage::TextFileRecoveryPage(QWidget* parent, AppSettings* settings)
    : QWidget(parent), m_ai(new MojibakeAi(settings, this)), m_analysisProgress(std::make_shared<std::atomic_int>(0)), m_settings(settings) {
    setAcceptDrops(true);
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content = new QWidget(scroll); content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false); scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(32, 12, 32, 28); layout->setSpacing(14);
    const auto button = [content](const char* name) { auto* b = new ElaPushButton(content); b->setObjectName(name); return b; };
    // Step 1: the file.
    auto* fileCard = card(content);
    auto* fileLayout = new QVBoxLayout(fileCard); fileLayout->setContentsMargins(18, 14, 18, 16); fileLayout->setSpacing(8);
    m_fileLabel = ui::label({}, 12, ui::Tone::Primary, fileCard);
    m_description = ui::label({}, 9, ui::Tone::Secondary, fileCard);
    m_open = button("fileRecoveryOpen"); ui::primaryButton(m_open); m_open->setMinimumWidth(120);
    m_analyze = button("fileRecoveryAnalyze"); m_cancel = button("fileRecoveryCancel");
    m_pathLabel = ui::label({}, 10, ui::Tone::Secondary, fileCard); m_pathLabel->setTextFormat(Qt::PlainText);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* actions = new QHBoxLayout; actions->setSpacing(8);
    actions->addWidget(m_open); actions->addWidget(m_analyze); actions->addWidget(m_cancel); actions->addSpacing(8); actions->addWidget(m_pathLabel, 1);
    m_sourceLabel = ui::label({}, 10, ui::Tone::Secondary, fileCard);
    m_source = new ElaComboBox(fileCard); m_source->setObjectName("fileRecoverySource"); m_source->addItem({}, -1);
    for (auto e : EncodingConverter::supported()) m_source->addItem(EncodingConverter::name(e), int(e));
    auto* sourceRow = new QHBoxLayout; sourceRow->addWidget(m_sourceLabel); sourceRow->addWidget(m_source); sourceRow->addStretch();
    m_status = ui::label({}, 10, ui::Tone::Secondary, fileCard); m_status->setObjectName("fileRecoveryStatus"); m_status->setTextFormat(Qt::PlainText);
    m_progress = new ElaProgressBar(fileCard); m_progress->setObjectName("fileRecoveryProgress");
    m_progress->setRange(0, 1000); m_progress->setTextVisible(false); m_progress->setFixedHeight(6); m_progress->hide();
    fileLayout->addWidget(m_fileLabel); fileLayout->addWidget(m_description); fileLayout->addLayout(actions);
    fileLayout->addLayout(sourceRow); fileLayout->addWidget(m_status); fileLayout->addWidget(m_progress);
    layout->addWidget(fileCard);
    // Step 2: choose a result and save a copy.
    auto* resultCard = card(content);
    auto* resultLayout = new QVBoxLayout(resultCard); resultLayout->setContentsMargins(18, 14, 18, 16); resultLayout->setSpacing(10);
    m_resultLabel = ui::label({}, 12, ui::Tone::Primary, resultCard);
    m_workspace = new RecoveryWorkspace("fileRecovery", "fileRecoveryPreview", resultCard);
    m_workspace->setPreviewLimit(PreviewCharacters);
    m_save = button("fileRecoverySave"); ui::primaryButton(m_save);
    m_aiAnalyze = button("fileRecoveryAiAnalyze");
    m_aiCancel = button("fileRecoveryAiCancel"); m_aiCancel->hide();
    m_detailsToggle = button("fileRecoveryDetailsToggle"); m_detailsToggle->setCheckable(true);
    auto* resultActions = new QHBoxLayout; resultActions->setSpacing(8);
    resultActions->addWidget(m_save); resultActions->addWidget(m_aiAnalyze); resultActions->addWidget(m_aiCancel);
    resultActions->addStretch(); resultActions->addWidget(m_detailsToggle);
    m_outputLabel = ui::label({}, 10, ui::Tone::Secondary, resultCard);
    m_output = new ElaComboBox(resultCard); m_output->setObjectName("fileRecoveryOutputEncoding");
    for (auto e : EncodingConverter::supported()) m_output->addItem(EncodingConverter::name(e), int(e));
    m_newlineLabel = ui::label({}, 10, ui::Tone::Secondary, resultCard);
    m_newlines = new ElaComboBox(resultCard); m_newlines->setObjectName("fileRecoveryNewlines");
    for (int i = 0; i < 3; ++i) m_newlines->addItem({}, i);
    m_bom = new ElaCheckBox(resultCard); m_bom->setObjectName("fileRecoveryOutputBom");
    auto* saveOptions = new QHBoxLayout; saveOptions->setSpacing(8);
    saveOptions->addWidget(m_outputLabel); saveOptions->addWidget(m_output); saveOptions->addSpacing(12);
    saveOptions->addWidget(m_newlineLabel); saveOptions->addWidget(m_newlines); saveOptions->addSpacing(12); saveOptions->addWidget(m_bom); saveOptions->addStretch();
    m_aiHint = ui::label({}, 9, ui::Tone::Secondary, resultCard); m_aiHint->setObjectName("fileRecoveryAiHint");
    m_aiStatus = ui::label({}, 10, ui::Tone::Secondary, resultCard); m_aiStatus->setObjectName("fileRecoveryAiStatus");
    m_aiProgress = new ElaProgressBar(resultCard); m_aiProgress->setObjectName("fileRecoveryAiProgress");
    m_aiProgress->setTextVisible(false); m_aiProgress->setFixedHeight(6); m_aiProgress->hide();
    m_detailsBox = new QWidget(resultCard);
    auto* detailsLayout = new QVBoxLayout(m_detailsBox); detailsLayout->setContentsMargins(0, 0, 0, 0); detailsLayout->setSpacing(8);
    m_details = new ElaPlainTextEdit(m_detailsBox); m_details->setObjectName("fileRecoveryDetails"); m_details->setReadOnly(true); m_details->setMinimumHeight(130);
    m_report = new DiagnosticReportWidget(m_detailsBox); m_report->setObjectName("fileDiagnosticReport");
    detailsLayout->addWidget(m_details); detailsLayout->addWidget(m_report);
    m_detailsBox->hide();
    resultLayout->addWidget(m_resultLabel); resultLayout->addWidget(m_workspace, 1); resultLayout->addLayout(resultActions);
    resultLayout->addLayout(saveOptions); resultLayout->addWidget(m_aiHint); resultLayout->addWidget(m_aiStatus);
    resultLayout->addWidget(m_aiProgress); resultLayout->addWidget(m_detailsBox);
    layout->addWidget(resultCard, 1);
    connect(m_open, &ElaPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open a text file"), {}, tr("Text files (*.txt *.csv *.log *.json *.ini *.xml *.yaml *.yml *.cpp *.h *.py *.md);;All files (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(m_analyze, &ElaPushButton::clicked, this, [this] { openFile(m_path); });
    connect(m_cancel, &ElaPushButton::clicked, this, [this] { if (m_cancelToken) m_cancelToken->store(true); });
    connect(m_source, &QComboBox::currentIndexChanged, this, [this] { if (!m_path.isEmpty()) openFile(m_path); });
    connect(m_output, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_workspace, &RecoveryWorkspace::selectionChanged, this, [this] { m_savedPath.clear(); m_saveError.clear(); refresh(); });
    connect(m_detailsToggle, &ElaPushButton::toggled, m_detailsBox, &QWidget::setVisible);
    connect(m_save, &ElaPushButton::clicked, this, [this] {
        const QFileInfo source(m_path);
        const auto suggested = source.dir().filePath(source.completeBaseName() + ".repaired" + (source.suffix().isEmpty() ? "" : "." + source.suffix()));
        QFileDialog dialog(this, tr("Create a repaired copy"), suggested);
        dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setOption(QFileDialog::DontConfirmOverwrite); // Backend always rejects existing paths.
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) exportCopy(dialog.selectedFiles().first());
    });
    connect(m_aiAnalyze, &ElaPushButton::clicked, this, &TextFileRecoveryPage::requestAi);
    connect(m_aiCancel, &ElaPushButton::clicked, m_ai, &MojibakeAi::cancel);
    connect(m_ai, &MojibakeAi::changed, this, &TextFileRecoveryPage::aiChanged);
    if (m_settings) connect(m_settings, &AppSettings::aiConfigurationChanged, this, [this] { m_ai->reset(); });
    m_progressPoll.setInterval(150);
    connect(&m_progressPoll, &QTimer::timeout, this, [this] {
        const int value = m_analysisProgress->load();
        m_progress->setVisible(value > 0); m_progress->setValue(value);
    });
    connect(&m_analysis, &QFutureWatcher<FileResult>::finished, this, [this] {
        m_progressPoll.stop(); m_progress->hide();
        m_result = m_analysis.result();
        if (m_resetOutput) {
            const QSignalBlocker block(m_output); m_output->setCurrentIndex(m_output->findData(int(Encoding::Utf8)));
            m_newlines->setCurrentIndex(0); m_bom->setChecked(m_result.bomEncoding.has_value());
        }
        refreshCards(); m_workspace->select(0); refresh();
    });
    connect(&m_export, &QFutureWatcher<SaveResult>::finished, this, [this] {
        const auto result = m_export.result(); m_saveError = result.error;
        m_savedPath = result.success ? m_pendingDestination : QString(); refresh();
    });
    retranslate();
}
TextFileRecoveryPage::~TextFileRecoveryPage() { if (m_cancelToken) m_cancelToken->store(true); m_ai->disconnect(this); m_ai->reset(); }
std::optional<Encoding> TextFileRecoveryPage::forcedEncoding() const {
    return m_source->currentData().toInt() < 0 ? std::nullopt : std::optional{Encoding(m_source->currentData().toInt())};
}
Encoding TextFileRecoveryPage::outputEncoding() const { return Encoding(m_output->currentData().toInt()); }
bool TextFileRecoveryPage::lossyTop() const { return !m_result.candidates.isEmpty() && lossy(m_result.candidates.first()); }
void TextFileRecoveryPage::openFile(const QString& path) {
    if (path.isEmpty() || m_analysis.isRunning() || m_export.isRunning()) return;
    m_ai->reset();
    m_resetOutput = m_path != path || m_result.bytes.isEmpty();
    m_path = path; m_result = {}; m_savedPath.clear(); m_saveError.clear();
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    m_analysisProgress = std::make_shared<std::atomic_int>(0);
    const auto cancel = m_cancelToken; const auto forced = forcedEncoding(); const auto progress = m_analysisProgress;
    m_progressPoll.start();
    m_analysis.setFuture(QtConcurrent::run([path, forced, cancel, progress] {
        return ByteEncodingAnalyzer::load(path, forced, cancel, [progress](double fraction) { progress->store(std::max(1, int(fraction * 1000))); });
    }));
    refreshCards(); refresh();
}
void TextFileRecoveryPage::exportCopy(const QString& destination) {
    const int selected = m_workspace->selected();
    if (destination.isEmpty() || m_analysis.isRunning() || m_export.isRunning() || selected < 0) return;
    auto snapshot = m_result; int index = selected;
    if (selected == RecoveryWorkspace::LocalSlots) {
        // The AI text is saved like a candidate; the source checks stay the same.
        FileCandidate ai; ai.text = m_ai->text(); ai.reversible = false;
        snapshot.candidates.append(ai); index = int(snapshot.candidates.size()) - 1;
    }
    const auto encoding = outputEncoding();
    const bool bom = m_bom->isEnabled() && m_bom->isChecked(); const auto newlines = Newlines(m_newlines->currentData().toInt());
    m_pendingDestination = destination; m_savedPath.clear(); m_saveError.clear();
    m_export.setFuture(QtConcurrent::run([snapshot, index, destination, encoding, bom, newlines] {
        return RecoveryOperation::saveCopy(snapshot, index, destination, encoding, bom, newlines);
    }));
    refresh();
}
void TextFileRecoveryPage::refreshCards() {
    QList<WorkspaceCandidate> cards;
    for (int i = 0; i < m_result.candidates.size(); ++i) {
        const auto& c = m_result.candidates[i];
        WorkspaceCandidate w;
        if (c.repairPath.isEmpty()) {
            w.text = c.text; w.score = c.score;
            w.title = tr("Decoded as %1").arg(EncodingConverter::name(c.encoding));
            QStringList aliases; for (auto e : c.aliases) aliases << EncodingConverter::name(e);
            w.subtitle = aliases.size() > 1 ? tr("Same text as %1").arg(aliases.join(", ")) : tr("Plain decoding, no repair");
            if (i == 0) w.badge = tr("Recommended");
        } else {
            Candidate repaired{c.text, c.repairPath, c.score, c.anomalies, c.reversible, c.evidence, c.lossy};
            w = MojibakePage::describe(repaired, i == 0);
        }
        cards << w;
    }
    WorkspaceCandidate ai;
    if (m_ai->hasResult()) {
        ai.title = m_ai->refined() ? tr("AI refined text") : tr("AI suggestion");
        ai.badge = "AI"; ai.text = m_ai->text(); ai.evidence = m_ai->evidence();
        ai.subtitle = m_ai->refined() ? tr("Checked against the surviving bytes") : tr("Semantic guess, not byte-verified");
    }
    m_workspace->setCandidates(cards, m_ai->hasResult() ? &ai : nullptr);
    m_aiRevision = m_ai->revision();
}
void TextFileRecoveryPage::refresh() {
    const bool busy = m_analysis.isRunning() || m_export.isRunning();
    const int selected = m_workspace->selected();
    const bool local = selected >= 0 && selected < m_result.candidates.size();
    m_report->setReport(busy ? DiagnosticReport{} : DiagnosticReport::file(m_result, local ? selected : (m_result.candidates.isEmpty() ? -1 : 0)));
    m_open->setEnabled(!busy); m_source->setEnabled(!busy); m_analyze->setEnabled(!busy && !m_path.isEmpty());
    m_save->setEnabled(!busy && (local || (selected == RecoveryWorkspace::LocalSlots && m_ai->hasResult())));
    m_output->setEnabled(!busy); m_newlines->setEnabled(!busy);
    m_cancel->setVisible(m_analysis.isRunning());
    const bool supportsBom = !ByteEncodingAnalyzer::bom(outputEncoding()).isEmpty();
    m_bom->setEnabled(!busy && supportsBom); if (!supportsBom) m_bom->setChecked(false);
    m_pathLabel->setText(m_path.isEmpty() ? tr("No file selected.") : m_path);
    if (m_analysis.isRunning()) m_status->setText(tr("Analyzing file bytes locally…"));
    else if (m_export.isRunning()) m_status->setText(tr("Creating the complete repaired copy…"));
    else if (!m_saveError.isEmpty()) m_status->setText(m_saveError);
    else if (!m_savedPath.isEmpty()) m_status->setText(tr("Created copy: %1. The source file was preserved.").arg(m_savedPath));
    else if (!m_result.error.isEmpty()) m_status->setText(tr("Could not read the file: %1").arg(m_result.error));
    else if (m_result.cancelled) m_status->setText(tr("File analysis cancelled."));
    else if (m_result.tooLarge) m_status->setText(tr("File exceeds the 10 MiB limit."));
    else if (m_result.invalidBom) m_status->setText(tr("The BOM conflicts with the selected encoding or the content cannot be decoded strictly. Try automatic detection."));
    else if (m_result.probablyBinary) m_status->setText(tr("This appears to be a binary file. Text recovery is not applicable."));
    else if (m_result.candidates.isEmpty()) m_status->setText(m_path.isEmpty() ? tr("Choose one text file. Analysis does not modify or upload it.") : tr("No valid text candidate for this encoding. Try automatic detection or another source encoding."));
    else if (lossyTop()) m_status->setText(tr("This file is GBK text that was once opened as UTF-8 and saved again; some bytes are gone. Certain and guessed characters are shown, lost ones are marked."));
    else m_status->setText(tr("Compare the candidates before exporting. No existing file will be overwritten."));
    showSelection();
}
void TextFileRecoveryPage::showSelection() {
    const int selected = m_workspace->selected();
    if (selected == RecoveryWorkspace::LocalSlots) { m_details->setPlainText(m_ai->explanation().join('\n')); updateAiControls(); return; }
    if (selected < 0 || selected >= m_result.candidates.size()) { m_details->clear(); updateAiControls(); return; }
    const auto& c = m_result.candidates[selected];
    QStringList aliases; for (auto e : c.aliases) aliases << EncodingConverter::name(e);
    const auto crlf = c.text.count("\r\n"), lf = c.text.count('\n') - crlf, cr = c.text.count('\r') - crlf;
    QStringList details{
        tr("Source bytes: %1; decoded characters: %2.").arg(m_result.bytes.size()).arg(c.text.size()),
        tr("Source encoding candidates: %1").arg(aliases.join(", ")),
        tr("BOM: %1").arg(m_result.bomEncoding ? EncodingConverter::name(*m_result.bomEncoding) : tr("None")),
        tr("Newlines: CRLF %1, LF %2, CR %3.").arg(crlf).arg(lf).arg(cr),
        tr("Exact inverse verified: %1").arg(c.reversible ? tr("Yes") : tr("No; partial recovery"))};
    if (m_result.preferredEncoding) details << tr("Byte evidence prefers %1. This does not prove the original meaning.").arg(EncodingConverter::name(*m_result.preferredEncoding));
    if (lossy(c)) details << MojibakePage::processLines(Candidate{c.text, c.repairPath, c.score, c.anomalies, c.reversible, c.evidence, c.lossy}, 0, false);
    else for (const auto& step : c.repairPath) details << tr("Text repair: encode as %1, then decode as %2.").arg(EncodingConverter::name(step.encodeAs), EncodingConverter::name(step.decodeAs));
    if (c.text.contains(QChar(0xfffd)) || !c.reversible) details << tr("Possible missing information. Unknown characters cannot be uniquely restored.");
    if (m_result.inverseRepairSkipped) details << tr("For large text, only byte encoding detection was performed; further mojibake search is limited to 1 MiB.");
    if (c.text.size() > m_workspace->shownCharacters()) details << tr("Preview shows the first %1 characters. Export includes the complete candidate.").arg(m_workspace->shownCharacters());
    details << tr("Scores rank possibilities, not probabilities. File contents are sent to AI only when you click the AI button.");
    m_details->setPlainText(details.join('\n'));
    updateAiControls();
}
void TextFileRecoveryPage::requestAi() {
    if (m_analysis.isRunning() || m_result.candidates.isEmpty() || m_ai->busy()) return;
    if (lossyTop()) {
        m_ai->refine(m_result.candidates.first().lossy);
        if (m_ai->hasResult()) m_workspace->select(RecoveryWorkspace::LocalSlots);
        return;
    }
    QList<Candidate> candidates;
    for (const auto& c : m_result.candidates) candidates << Candidate{c.text, c.repairPath, c.score, c.anomalies, c.reversible};
    // The plain decoding is what the user would see when opening the file.
    const auto plain = std::find_if(m_result.candidates.begin(), m_result.candidates.end(), [](const auto& c) { return c.repairPath.isEmpty(); });
    const auto original = plain != m_result.candidates.end() ? plain->text : m_result.candidates.first().text;
    m_ai->suggest(original, candidates, original.contains(QChar(0xfffd)));
}
void TextFileRecoveryPage::aiChanged() {
    if (m_ai->revision() != m_aiRevision) {
        const bool first = !m_workspace->card(RecoveryWorkspace::LocalSlots)->isEnabled();
        refreshCards();
        if (first && m_ai->hasResult()) m_workspace->select(RecoveryWorkspace::LocalSlots);
        refresh();
        return;
    }
    if (m_workspace->selected() == RecoveryWorkspace::LocalSlots) m_details->setPlainText(m_ai->explanation().join('\n'));
    updateAiControls();
}
void TextFileRecoveryPage::updateAiControls() {
    const bool configured = m_ai->configured(), busy = m_ai->busy();
    const bool lossyFile = lossyTop();
    const int chunks = lossyFile ? MojibakeAi::chunkCount(*m_result.candidates.first().lossy) : 0;
    const auto plain = std::find_if(m_result.candidates.begin(), m_result.candidates.end(), [](const auto& c) { return c.repairPath.isEmpty(); });
    const bool small = !m_result.candidates.isEmpty() &&
        (plain != m_result.candidates.end() ? plain->text : m_result.candidates.first().text).toUtf8().size() <= MaxSuggestionBytes;
    m_aiAnalyze->setText(lossyFile ? MojibakePage::tr("AI refinement (%n chunk(s))", nullptr, chunks) : MojibakePage::tr("AI deep analysis"));
    m_aiAnalyze->setEnabled(configured && !busy && !m_analysis.isRunning() && (lossyFile ? chunks > 0 : small));
    m_aiCancel->setVisible(busy);
    if (!configured) m_aiHint->setText(MojibakePage::tr("Configure the AI API address and model in Settings first. Local analysis does not upload text."));
    else if (lossyFile) m_aiHint->setText(MojibakePage::tr("AI refinement sends the marked local draft in %n chunk(s) to %1, three at a time. Each reply is checked against the surviving bytes; this may take a while and cost API fees.", nullptr, chunks).arg(m_ai->target()));
    else if (m_result.candidates.isEmpty()) m_aiHint->setText(tr("Analyze a file first. AI is used only when you click it."));
    else if (!small) m_aiHint->setText(tr("AI suggestions are limited to 16 KiB of text. Damaged GBK files can be refined in chunks of any size."));
    else m_aiHint->setText(tr("Clicking AI analysis sends the decoded text and local candidates to %1.").arg(m_ai->target()));
    m_aiStatus->setText(m_ai->status()); m_aiStatus->setVisible(!m_ai->status().isEmpty());
    m_aiProgress->setVisible(busy && m_ai->refined());
    m_aiProgress->setRange(0, std::max(1, m_ai->progressMaximum())); m_aiProgress->setValue(m_ai->progress());
}
void TextFileRecoveryPage::dragEnterEvent(QDragEnterEvent* event) {
    if (!singleFile(event->mimeData()).isEmpty()) event->acceptProposedAction();
}
void TextFileRecoveryPage::dropEvent(QDropEvent* event) {
    const auto path = singleFile(event->mimeData());
    if (path.isEmpty()) return;
    event->acceptProposedAction();
    openFile(path);
}
void TextFileRecoveryPage::retranslate() {
    m_fileLabel->setText(tr("1  Choose a text file"));
    m_description->setText(tr("Choose or drop one file. Its bytes are analyzed locally; the source file is never modified, and a repaired copy is saved separately."));
    m_resultLabel->setText(tr("2  Choose a result and save a copy"));
    m_open->setText(tr("Choose file")); m_analyze->setText(tr("Analyze again")); m_cancel->setText(tr("Cancel")); m_save->setText(tr("Save repaired copy…"));
    m_detailsToggle->setText(tr("Details"));
    m_sourceLabel->setText(tr("Source encoding")); m_source->setItemText(0, tr("Automatic comparison"));
    m_outputLabel->setText(tr("Output encoding")); m_newlineLabel->setText(tr("Output newlines"));
    m_newlines->setItemText(0, tr("Preserve original")); m_newlines->setItemText(1, "LF"); m_newlines->setItemText(2, "CRLF");
    m_bom->setText(tr("Write BOM (defaults to source BOM presence)"));
    m_aiCancel->setText(MojibakePage::tr("Cancel AI"));
    m_workspace->setPlaceholder(tr("Results appear here after a file is analyzed."));
    refreshCards(); refresh();
}
void TextFileRecoveryPage::changeEvent(QEvent* event) { QWidget::changeEvent(event); if (event->type() == QEvent::LanguageChange) retranslate(); }
