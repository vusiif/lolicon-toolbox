#include "MojibakePage.h"
#include "MojibakeAnalyzer.h"
#include "LossyGbkRecovery.h"
#include "MojibakeAi.h"
#include "RecoveryWorkspace.h"
#include "TextFileRecoveryPage.h"
#include "FilenameRecoveryPage.h"
#include "ClipboardSourcePage.h"
#include "DiagnosticReportWidget.h"
#include "UiTheme.h"
#include "services/AppSettings.h"
#include <ElaPlainTextEdit.h>
#include <ElaProgressBar.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QMimeData>
#include <QStackedWidget>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtConcurrentRun>
using namespace mojibake;
namespace {
QFrame* card(QWidget* parent) { auto* frame = new QFrame(parent); ui::styleCard(frame); return frame; }
// Mode buttons look like segments: the current one uses the primary colour.
void styleMode(ElaPushButton* b, bool selected) {
    for (auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        const auto base = eTheme->getThemeColor(mode, selected ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase);
        const auto hover = eTheme->getThemeColor(mode, selected ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover);
        const auto press = eTheme->getThemeColor(mode, selected ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress);
        const auto text = eTheme->getThemeColor(mode, selected ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText);
        if (mode == ElaThemeType::Light) { b->setLightDefaultColor(base); b->setLightHoverColor(hover); b->setLightPressColor(press); b->setLightTextColor(text); }
        else { b->setDarkDefaultColor(base); b->setDarkHoverColor(hover); b->setDarkPressColor(press); b->setDarkTextColor(text); }
    }
    b->update();
}
bool lossy(const Candidate& c) { return !c.path.isEmpty() && c.path.last().lossy; }
}
WorkspaceCandidate MojibakePage::describe(const Candidate& c, const bool recommended) {
    WorkspaceCandidate w;
    w.text = c.text; w.evidence = c.evidence; w.score = c.score;
    if (c.path.isEmpty()) { w.title = tr("Original text"); w.subtitle = tr("No conversion"); }
    else if (lossy(c)) {
        w.title = tr("Rebuilt damaged GBK text");
        w.subtitle = tr("%1 certain · %2 guessed · %3 lost").arg(c.evidence.count(static_cast<char>(CharEvidence::Exact)))
                         .arg(c.evidence.count(static_cast<char>(CharEvidence::Partial))).arg(c.evidence.count(static_cast<char>(CharEvidence::Unknown)));
    } else {
        QStringList steps;
        bool partial = false;
        // Each step encodes as A and decodes as B: the bytes were B, misread as A.
        for (const auto& step : c.path) {
            steps << tr("%1 read as %2").arg(EncodingConverter::name(step.decodeAs), EncodingConverter::name(step.encodeAs));
            partial = partial || step.partial;
        }
        w.title = steps.join(" · ");
        w.subtitle = partial ? tr("Partly readable; lost bytes stay marked") : c.reversible ? tr("Exact inverse verified") : tr("Inverse not proven");
    }
    if (recommended) w.badge = tr("Recommended");
    else if (c.path.isEmpty()) { w.badge = tr("Original"); w.accentBadge = false; }
    return w;
}
QStringList MojibakePage::processLines(const Candidate& candidate, const int examined, const bool limited) {
    QStringList path;
    path << tr("Quality score: %1 / 100 (not a probability).").arg(candidate.score, 0, 'f', 1);
    if (lossy(candidate)) {
        const auto& e = candidate.evidence;
        path << tr("The original bytes were GBK. They were read as UTF-8, which replaced every invalid byte sequence with U+FFFD and lost those bytes; the file was then saved again.");
        path << tr("Every byte that formed valid UTF-8 survived. The tool searched GBK byte sequences that reproduce the damaged text exactly and ranked them with a character frequency model (jieba dictionary).");
        path << tr("Certain characters: %1 (both bytes survived). Guessed: %2 (one byte survived; the model chose the other). Lost: %3 (no byte survived), shown as □.")
                    .arg(e.count(static_cast<char>(CharEvidence::Exact))).arg(e.count(static_cast<char>(CharEvidence::Partial))).arg(e.count(static_cast<char>(CharEvidence::Unknown)));
        if (candidate.lossy) path << (candidate.lossy->replacement == Utf8Replacement::PerByte
            ? tr("The damage matches a decoder that writes one U+FFFD per invalid byte (for example Qt or .NET).")
            : tr("The damage matches a decoder that writes one U+FFFD per invalid sequence (for example browsers, Java or Python)."));
        path << tr("AI refinement can fill lost characters from context; it is checked against the surviving bytes, but filled characters remain guesses.");
        return path;
    }
    path << tr("Ranking uses anomaly density (encoding errors and uncommon CJK characters), conversion count, a preference for UTF-8 decoding, and penalties for incomplete recovery.");
    if (candidate.path.isEmpty()) path << tr("Original text: no conversion applied.");
    for (int i = 0; i < candidate.path.size(); ++i) {
        const auto& step = candidate.path[i];
        path << tr("Step %1: encode as %2, then decode as %3.").arg(i + 1)
            .arg(EncodingConverter::name(step.encodeAs), EncodingConverter::name(step.decodeAs));
        if (step.segmented) path << tr("Only representable text segments were changed; other text was preserved.");
        if (step.partial) path << tr("Readable sections were recovered. Unknown byte regions are marked with U+FFFD; their original content and length cannot be determined.");
    }
    path << tr("Exact inverse verified: %1").arg(candidate.reversible ? tr("Yes") : tr("Not proven for the complete text"));
    path << tr("Detected anomaly count: %1").arg(candidate.anomalies);
    path << tr("Search depth: at most 2; examined %1 unique candidates.").arg(examined);
    if (limited) path << tr("The bounded search was pruned; other possibilities may exist.");
    path << tr("A reversible encoding path does not prove that the meaning is correct.");
    return path;
}
MojibakePage::MojibakePage(QWidget* parent, AppSettings* settings)
    : ToolPage(parent), m_ai(new MojibakeAi(settings, this)), m_analysisProgress(std::make_shared<std::atomic_int>(0)), m_settings(settings) {
    setObjectName("mojibakePage");
    m_clipboardWriter = [](const QString& text) { auto* clipboard = QApplication::clipboard(); clipboard->setText(text); return clipboard->ownsClipboard(); };
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    // Header: what the tool does, then where the text comes from.
    auto* header = new QVBoxLayout; header->setContentsMargins(32, 20, 32, 8); header->setSpacing(6);
    m_title = ui::label({}, 18, ui::Tone::Primary, this);
    m_description = ui::label({}, 10, ui::Tone::Secondary, this);
    header->addWidget(m_title); header->addWidget(m_description);
    auto* modes = new QHBoxLayout; modes->setSpacing(6); modes->setContentsMargins(0, 6, 0, 0);
    m_modeGroup = new QButtonGroup(this);
    for (int i = 0; i < 4; ++i) {
        constexpr std::array<const char*, 4> modeNames{"mojibakeTextMode", "mojibakeFileMode", "mojibakeNameMode", "mojibakeClipboardMode"};
        auto* b = new ElaPushButton(this); b->setObjectName(modeNames[i]); b->setCheckable(true);
        m_modes[i] = b; m_modeGroup->addButton(b, i); modes->addWidget(b);
    }
    modes->addStretch(); header->addLayout(modes);
    outer->addLayout(header);
    m_pages = new QStackedWidget(this); m_pages->setObjectName("mojibakeSources");
    outer->addWidget(m_pages, 1);
    // ------------------------------------------------------ pasted text
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 12, 32, 28); layout->setSpacing(14);
    const auto button = [content](const char* name) { auto* b = new ElaPushButton(content); b->setObjectName(name); return b; };
    // Step 1: input.
    auto* inputCard = card(content);
    auto* inputLayout = new QVBoxLayout(inputCard); inputLayout->setContentsMargins(18, 14, 18, 16); inputLayout->setSpacing(8);
    m_inputLabel = ui::label({}, 12, ui::Tone::Primary, inputCard);
    m_inputHint = ui::label({}, 9, ui::Tone::Secondary, inputCard);
    m_input = new ElaPlainTextEdit(inputCard); m_input->setObjectName("mojibakeInput");
    m_input->setMinimumHeight(110); m_input->setMaximumHeight(200);  // The results deserve the space.
    m_input->viewport()->installEventFilter(this);
    m_analyze = button("mojibakeAnalyze"); ui::primaryButton(m_analyze); m_analyze->setMinimumWidth(120);
    m_cancel = button("mojibakeCancel"); m_cancel->hide();
    m_clear = button("mojibakeClear");
    m_status = ui::label({}, 10, ui::Tone::Secondary, inputCard); m_status->setObjectName("mojibakeStatus");
    m_progress = new ElaProgressBar(inputCard); m_progress->setObjectName("mojibakeProgress");
    m_progress->setRange(0, 1000); m_progress->setTextVisible(false); m_progress->setFixedHeight(6); m_progress->hide();
    auto* actions = new QHBoxLayout; actions->setSpacing(8);
    actions->addWidget(m_analyze); actions->addWidget(m_cancel); actions->addWidget(m_clear); actions->addSpacing(8); actions->addWidget(m_status, 1);
    inputLayout->addWidget(m_inputLabel); inputLayout->addWidget(m_inputHint); inputLayout->addWidget(m_input, 1);
    inputLayout->addLayout(actions); inputLayout->addWidget(m_progress);
    layout->addWidget(inputCard);
    // Step 2: choose and use a result.
    auto* resultCard = card(content);
    auto* resultLayout = new QVBoxLayout(resultCard); resultLayout->setContentsMargins(18, 14, 18, 16); resultLayout->setSpacing(10);
    m_outputLabel = ui::label({}, 12, ui::Tone::Primary, resultCard);
    m_workspace = new RecoveryWorkspace("mojibake", "mojibakeOutput", resultCard);
    m_copy = button("mojibakeCopy"); ui::primaryButton(m_copy); m_copy->setEnabled(false);
    m_aiAnalyze = button("mojibakeAiAnalyze");
    m_aiCancel = button("mojibakeAiCancel"); m_aiCancel->hide();
    m_details = button("mojibakeDetails"); m_details->setCheckable(true); m_details->setEnabled(false);
    auto* resultActions = new QHBoxLayout; resultActions->setSpacing(8);
    resultActions->addWidget(m_copy); resultActions->addWidget(m_aiAnalyze); resultActions->addWidget(m_aiCancel);
    resultActions->addStretch(); resultActions->addWidget(m_details);
    m_aiHint = ui::label({}, 9, ui::Tone::Secondary, resultCard); m_aiHint->setObjectName("mojibakeAiHint");
    m_aiStatus = ui::label({}, 10, ui::Tone::Secondary, resultCard); m_aiStatus->setObjectName("mojibakeAiStatus");
    m_aiProgress = new ElaProgressBar(resultCard); m_aiProgress->setObjectName("mojibakeAiProgress");
    m_aiProgress->setTextVisible(false); m_aiProgress->setFixedHeight(6); m_aiProgress->hide();
    m_detailsBox = new QWidget(resultCard);
    auto* detailsLayout = new QVBoxLayout(m_detailsBox); detailsLayout->setContentsMargins(0, 0, 0, 0); detailsLayout->setSpacing(8);
    m_process = new ElaPlainTextEdit(m_detailsBox); m_process->setObjectName("mojibakeProcess"); m_process->setReadOnly(true); m_process->setMinimumHeight(150);
    m_report = new DiagnosticReportWidget(m_detailsBox); m_report->setObjectName("textDiagnosticReport");
    detailsLayout->addWidget(m_process); detailsLayout->addWidget(m_report);
    m_detailsBox->hide();
    resultLayout->addWidget(m_outputLabel); resultLayout->addWidget(m_workspace, 1); resultLayout->addLayout(resultActions);
    resultLayout->addWidget(m_aiHint); resultLayout->addWidget(m_aiStatus); resultLayout->addWidget(m_aiProgress); resultLayout->addWidget(m_detailsBox);
    layout->addWidget(resultCard, 1);
    m_pages->addWidget(scroll);
    // ------------------------------------------------------ other sources
    m_filePage = new TextFileRecoveryPage(m_pages, settings);
    m_pages->addWidget(m_filePage);
    m_pages->addWidget(new FilenameRecoveryPage(m_pages));
    auto* clipboardPage = new ClipboardSourcePage(m_pages);
    m_pages->addWidget(clipboardPage);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](const int id) { selectMode(id); });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { selectMode(m_pages->currentIndex()); });
    connect(clipboardPage, &ClipboardSourcePage::textChosen, this, [this](const QString& text) {
        m_input->setPlainText(text); invalidate(); selectMode(0);
        m_status->setText(tr("Selected clipboard text loaded. Run local analysis before requesting AI."));
    });
    // ------------------------------------------------------ behaviour
    connect(m_analyze, &ElaPushButton::clicked, this, &MojibakePage::analyze);
    connect(m_clear, &ElaPushButton::clicked, this, [this] { m_input->clear(); invalidate(); });
    connect(m_cancel, &ElaPushButton::clicked, this, [this] { if (m_cancellation) m_cancellation->store(true); });
    connect(m_input, &ElaPlainTextEdit::textChanged, this, &MojibakePage::invalidate);
    connect(m_workspace, &RecoveryWorkspace::selectionChanged, this, [this] { m_copied = false; showSelection(); });
    connect(m_details, &ElaPushButton::toggled, m_detailsBox, &QWidget::setVisible);
    connect(m_copy, &ElaPushButton::clicked, this, [this] {
        const auto text = selectedText();
        if (text.isEmpty()) return;
        m_copied = m_clipboardWriter(text);
        showResult();
    });
    connect(m_aiAnalyze, &ElaPushButton::clicked, this, &MojibakePage::requestAi);
    connect(m_aiCancel, &ElaPushButton::clicked, m_ai, &MojibakeAi::cancel);
    connect(m_ai, &MojibakeAi::changed, this, &MojibakePage::aiChanged);
    if (m_settings) connect(m_settings, &AppSettings::aiConfigurationChanged, this, [this] { m_ai->reset(); });
    m_progressPoll.setInterval(150);
    connect(&m_progressPoll, &QTimer::timeout, this, [this] {
        const int value = m_analysisProgress->load();
        m_progress->setVisible(value > 0); m_progress->setValue(value);
    });
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        m_progressPoll.stop(); m_progress->hide();
        m_analyze->setEnabled(true); m_cancel->hide();
        if (m_stale || inputText() != m_submitted) { m_result = {}; showResult(); return; }
        m_result = m_watcher.result();
        m_workspace->select(0);
        showResult();
    });
    selectMode(0);
    retranslate();
}
MojibakePage::~MojibakePage() { if (m_cancellation) m_cancellation->store(true); static_cast<void>(m_ai->disconnect(this)); m_ai->reset(); }
void MojibakePage::selectMode(const int index) {
    m_pages->setCurrentIndex(index);
    for (int i = 0; i < 4; ++i) { m_modes[i]->setChecked(i == index); styleMode(m_modes[i], i == index); }
}
QString MojibakePage::inputText() const {
    // QTextDocument::toPlainText normalizes NBSP to ASCII spaces, destroying
    // byte evidence in common UTF-8-as-Latin-1 mojibake such as "ä½ å¥½".
    return m_input->document()->toRawText().replace(QChar::ParagraphSeparator, '\n')
        .replace(QChar::LineSeparator, '\n');
}
QString MojibakePage::selectedText() const {
    const int index = m_workspace->selected();
    if (index == RecoveryWorkspace::LocalSlots) return m_ai->text();
    return index >= 0 && index < m_result.candidates.size() ? m_result.candidates[index].text : QString();
}
void MojibakePage::invalidate() {
    m_ai->reset();
    m_stale = true; m_copied = false;
    if (m_cancellation) m_cancellation->store(true);
    m_result = {};
    m_details->setChecked(false);
    if (!m_watcher.isRunning()) showResult();
    else { m_workspace->setCandidates({}, nullptr); m_process->clear(); m_report->setReport({}); }
}
void MojibakePage::analyze() {
    if (m_watcher.isRunning()) return;
    m_submitted = inputText();
    invalidate();
    m_stale = false;
    if (m_submitted.isEmpty()) { m_status->setText(tr("Enter text to analyze.")); return; }
    m_analyze->setEnabled(false); m_cancel->show();
    m_status->setText(tr("Analyzing locally…"));
    m_cancellation = std::make_shared<std::atomic_bool>(false);
    m_analysisProgress = std::make_shared<std::atomic_int>(0);
    m_progressPoll.start();
    const auto text = m_submitted;
    const auto cancel = m_cancellation;
    const auto progress = m_analysisProgress;
    // No QWidget or page pointer is captured by the worker.
    m_watcher.setFuture(QtConcurrent::run([text, cancel, progress] {
        return MojibakeAnalyzer::analyze(text, cancel, [progress](const double fraction) { progress->store(std::max(1, static_cast<int>(fraction * 1000))); });
    }));
}
void MojibakePage::refreshCards() {
    QList<WorkspaceCandidate> cards;
    for (int i = 0; i < m_result.candidates.size(); ++i) cards << describe(m_result.candidates[i], i == 0 && m_result.likelyMojibake);
    WorkspaceCandidate ai;
    if (m_ai->hasResult()) {
        ai.title = m_ai->refined() ? tr("AI refined text") : tr("AI suggestion");
        ai.badge = "AI"; ai.text = m_ai->text(); ai.evidence = m_ai->evidence();
        ai.subtitle = m_ai->refined() ? tr("Checked against the surviving bytes") : tr("Semantic guess, not byte-verified");
    }
    m_workspace->setCandidates(cards, m_ai->hasResult() ? &ai : nullptr);
    m_aiRevision = m_ai->revision();
}
void MojibakePage::showResult() {
    refreshCards();
    ui::setTone(m_status, ui::Tone::Secondary);
    if (m_watcher.isRunning()) m_status->setText(tr("Analyzing locally…"));
    else if (m_result.tooLarge) m_status->setText(tr("Input exceeds the 1 MiB limit. Analyze a smaller section."));
    else if (m_result.cancelled) m_status->setText(tr("Analysis cancelled."));
    else if (m_result.candidates.isEmpty()) m_status->setText(tr("Paste text, then analyze. Scores rank possibilities; they are not probabilities."));
    else if (lossy(m_result.candidates.first())) m_status->setText(tr("Damaged GBK text detected. Characters whose bytes were lost are marked; AI refinement can fill them from context."));
    else if (m_result.possibleLoss) m_status->setText(tr("Possible missing information: replacement characters or repeated question marks. Recovery is uncertain."));
    else if (!m_result.likelyMojibake) m_status->setText(tr("No reliable improvement found. The original text is retained; alternatives may be incorrect."));
    else if (m_result.confidence == Confidence::High) m_status->setText(tr("High confidence candidate. Check the meaning before using it."));
    else m_status->setText(tr("Possible recovery. Several encodings may explain the same bytes; compare the candidates."));
    if (m_copied) m_status->setText(m_status->text() + " " + tr("Copied to clipboard."));
    m_details->setEnabled(!m_result.candidates.isEmpty());
    showSelection();
}
void MojibakePage::showSelection() {
    const int index = m_workspace->selected();
    const bool local = index >= 0 && index < m_result.candidates.size();
    m_copy->setEnabled(!selectedText().isEmpty() && !m_watcher.isRunning());
    m_report->setReport(m_watcher.isRunning() ? DiagnosticReport{} : DiagnosticReport::text(m_result, local ? index : m_result.candidates.isEmpty() ? -1 : 0));
    if (index == RecoveryWorkspace::LocalSlots) m_process->setPlainText(m_ai->explanation().join('\n'));
    else if (local) m_process->setPlainText(processLines(m_result.candidates[index], m_result.generated, m_result.searchLimited).join('\n'));
    else m_process->clear();
    updateAiControls();
}
void MojibakePage::requestAi() {
    if (m_watcher.isRunning() || m_result.candidates.isEmpty() || m_ai->busy()) return;
    if (const auto& top = m_result.candidates.first(); lossy(top) && top.lossy) m_ai->refine(top.lossy);
    else m_ai->suggest(m_submitted, m_result.candidates, m_result.possibleLoss);
    // A refinement shows its partial result at once: put it in front of the user.
    if (m_ai->refined() && m_ai->hasResult()) m_workspace->select(RecoveryWorkspace::LocalSlots);
}
void MojibakePage::aiChanged() {
    if (m_ai->revision() != m_aiRevision) {
        const bool first = !m_workspace->card(RecoveryWorkspace::LocalSlots)->isEnabled();
        refreshCards();
        // A finished suggestion is shown right away; later updates keep the user's choice.
        if (first && m_ai->hasResult()) m_workspace->select(RecoveryWorkspace::LocalSlots);
        if (m_workspace->selected() == RecoveryWorkspace::LocalSlots) m_process->setPlainText(m_ai->explanation().join('\n'));
        m_copy->setEnabled(!selectedText().isEmpty() && !m_watcher.isRunning());
    } else if (m_workspace->selected() == RecoveryWorkspace::LocalSlots) m_process->setPlainText(m_ai->explanation().join('\n'));
    updateAiControls();
}
void MojibakePage::updateAiControls() {
    const bool configured = m_ai->configured();
    const bool busy = m_ai->busy();
    const bool lossyTop = !m_result.candidates.isEmpty() && lossy(m_result.candidates.first()) && m_result.candidates.first().lossy;
    const int chunks = lossyTop ? MojibakeAi::chunkCount(*m_result.candidates.first().lossy) : 0;
    m_aiAnalyze->setText(lossyTop ? tr("AI refinement (%n chunk(s))", nullptr, chunks) : tr("AI deep analysis"));
    m_aiAnalyze->setEnabled(configured && !busy && !m_watcher.isRunning() && !m_result.candidates.isEmpty() && (!lossyTop || chunks > 0));
    m_aiCancel->setVisible(busy);
    if (!configured) m_aiHint->setText(tr("Configure the AI API address and model in Settings first. Local analysis does not upload text."));
    else if (lossyTop) m_aiHint->setText(tr("AI refinement sends the marked local draft in %n chunk(s) to %1, three at a time. Each reply is checked against the surviving bytes; this may take a while and cost API fees.", nullptr, chunks).arg(m_ai->target()));
    else m_aiHint->setText(tr("Clicking AI analysis sends the original text and local candidates to %1 using model %2. Suggestions may contain inferred characters. Run local analysis first.")
                               .arg(m_settings ? m_settings->aiConfiguration().baseUrl : QString(), m_settings ? m_settings->aiConfiguration().model : QString()));
    m_aiStatus->setText(m_ai->status());
    m_aiStatus->setVisible(!m_ai->status().isEmpty());
    m_aiProgress->setVisible(busy && m_ai->refined());
    m_aiProgress->setRange(0, std::max(1, m_ai->progressMaximum())); m_aiProgress->setValue(m_ai->progress());
}bool MojibakePage::openDroppedFile(const QMimeData* mime) {
    if (!mime || !mime->hasUrls() || mime->urls().size() != 1 || !mime->urls().first().isLocalFile()) return false;
    selectMode(1);
    m_filePage->openFile(mime->urls().first().toLocalFile());
    return true;
}
bool MojibakePage::eventFilter(QObject* watched, QEvent* event) {
    // A file dropped on the text box opens in the file mode instead of pasting its path.
    if (watched == m_input->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            if (auto* drag = static_cast<QDragMoveEvent*>(event); drag->mimeData()->hasUrls() && drag->mimeData()->urls().size() == 1 && drag->mimeData()->urls().first().isLocalFile()) { drag->acceptProposedAction(); return true; }
        } else if (event->type() == QEvent::Drop) {
            if (auto* drop = static_cast<QDropEvent*>(event); openDroppedFile(drop->mimeData())) { drop->acceptProposedAction(); return true; }
        }
    }
    return ToolPage::eventFilter(watched, event);
}
void MojibakePage::retranslate() {
    m_title->setText(tr("Mojibake Repair"));
    m_description->setText(tr("Recover text that was saved or opened with the wrong encoding. Local analysis stays offline; AI is used only when you click it."));
    m_modes[0]->setText(tr("Pasted text")); m_modes[1]->setText(tr("Text file"));
    m_modes[2]->setText(tr("File and folder names")); m_modes[3]->setText(tr("Clipboard"));
    for (auto* b : m_modes) b->setFixedWidth(std::max(110, b->fontMetrics().horizontalAdvance(b->text()) + 36));
    m_inputLabel->setText(tr("1  Garbled text")); m_inputHint->setText(tr("Paste the text exactly as it looks. You can also drop a text file here."));
    m_outputLabel->setText(tr("2  Choose a result"));
    m_analyze->setText(tr("Analyze")); m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear"));
    m_copy->setText(tr("Copy result")); m_details->setText(tr("Recovery process"));
    m_input->setPlaceholderText(tr("Paste garbled text here…"));
    m_workspace->setPlaceholder(tr("Results appear here after analysis."));
    m_aiCancel->setText(tr("Cancel AI"));
    showResult();
}
void MojibakePage::changeEvent(QEvent* event) {
    ToolPage::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
