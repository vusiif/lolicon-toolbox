#include "MojibakePage.h"
#include "MojibakeAnalyzer.h"
#include "TextFileRecoveryPage.h"
#include "FilenameRecoveryPage.h"
#include "ClipboardSourcePage.h"
#include "DiagnosticReportWidget.h"
#include <QStackedWidget>
#include "UiTheme.h"
#include "services/AppSettings.h"
#include "ai/AiService.h"
#include <QJsonArray>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaTheme.h>
#include <QButtonGroup>
#include <ElaScrollArea.h>
#include <QtConcurrentRun>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QClipboard>
#include <QApplication>
#include <QTextDocument>
using namespace mojibake;
MojibakePage::MojibakePage(QWidget* parent, AppSettings* settings) : ToolPage(parent), m_settings(settings), m_aiService(new ai::AiService(this)) {
    setObjectName("mojibakePage");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    auto* modes = new QHBoxLayout; modes->setContentsMargins(32, 12, 32, 0);
    m_textMode = new ElaPushButton(this); m_textMode->setObjectName("mojibakeTextMode");
    m_fileMode = new ElaPushButton(this); m_fileMode->setObjectName("mojibakeFileMode");
    m_nameMode = new ElaPushButton(this); m_nameMode->setObjectName("mojibakeNameMode");
    m_clipboardMode = new ElaPushButton(this); m_clipboardMode->setObjectName("mojibakeClipboardMode");
    modes->addWidget(m_textMode); modes->addWidget(m_fileMode); modes->addWidget(m_nameMode); modes->addWidget(m_clipboardMode); modes->addStretch(); outer->addLayout(modes);
    auto* pages = new QStackedWidget(this); pages->setObjectName("mojibakeSources");
    pages->addWidget(scroll); pages->addWidget(new TextFileRecoveryPage(pages)); pages->addWidget(new FilenameRecoveryPage(pages)); outer->addWidget(pages);
    auto* clipboardPage = new ClipboardSourcePage(pages); pages->addWidget(clipboardPage);
    m_textMode->setEnabled(false);
    auto selectMode = [this, pages](int index) {
        pages->setCurrentIndex(index); m_textMode->setEnabled(index != 0); m_fileMode->setEnabled(index != 1); m_nameMode->setEnabled(index != 2);
        m_clipboardMode->setEnabled(index != 3);
    };
    connect(m_textMode, &ElaPushButton::clicked, this, [selectMode] { selectMode(0); });
    connect(m_fileMode, &ElaPushButton::clicked, this, [selectMode] { selectMode(1); });
    connect(m_nameMode, &ElaPushButton::clicked, this, [selectMode] { selectMode(2); });
    connect(m_clipboardMode, &ElaPushButton::clicked, this, [selectMode] { selectMode(3); });
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(12);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    m_title = ui::label({}, 18, ui::Tone::Primary, content);
    m_description = ui::label({}, 10, ui::Tone::Secondary, content);
    m_inputLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_outputLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_status = ui::label({}, 10, ui::Tone::Secondary, content);
    m_status->setObjectName("mojibakeStatus");
    auto editor = [content](const char* name, bool readOnly) {
        auto* edit = new ElaPlainTextEdit(content);
        edit->setObjectName(name);
        edit->setReadOnly(readOnly);
        edit->setMinimumHeight(100);
        return edit;
    };
    m_input = editor("mojibakeInput", false);
    connect(clipboardPage, &ClipboardSourcePage::textChosen, this, [this, selectMode](const QString& text) {
        m_input->setPlainText(text); invalidate(); selectMode(0);
        m_status->setText(tr("Selected clipboard text loaded. Run local analysis before requesting AI."));
    });
    m_output = editor("mojibakeOutput", true);
    m_process = editor("mojibakeProcess", true);
    m_report = new DiagnosticReportWidget(content); m_report->setObjectName("textDiagnosticReport");
    m_process->hide();
    auto button = [content](const char* name) {
        auto* b = new ElaPushButton(content);
        b->setObjectName(name);
        return b;
    };
    m_analyze = button("mojibakeAnalyze"); ui::primaryButton(m_analyze);
    m_clear = button("mojibakeClear");
    m_cancel = button("mojibakeCancel"); m_cancel->hide();
    m_copy = button("mojibakeCopy"); m_copy->setEnabled(false);
    m_details = button("mojibakeDetails"); m_details->setCheckable(true); m_details->setEnabled(false);
    m_candidates = new QButtonGroup(this);
    m_candidates->setObjectName("mojibakeCandidates");
    m_candidates->setExclusive(true);
    auto* candidateRow = new QHBoxLayout;
    candidateRow->setSpacing(8);
    for (int i = 0; i < 5; ++i) {
        auto* b = button(qPrintable(QString("mojibakeCandidate%1").arg(i + 1)));
        b->setCheckable(true);
        b->setEnabled(false);
        b->setFixedHeight(54);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto font = b->font(); font.setPixelSize(12); b->setFont(font);
        m_candidateButtons[i] = b;
        m_candidates->addButton(b, i);
        candidateRow->addWidget(b, 1);
    }
    auto* actions = new QHBoxLayout;
    actions->addWidget(m_analyze); actions->addWidget(m_cancel); actions->addWidget(m_clear); actions->addStretch();
    auto* resultActions = new QHBoxLayout;
    resultActions->addWidget(m_copy); resultActions->addWidget(m_details); resultActions->addStretch();
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addWidget(m_inputLabel);
    layout->addWidget(m_input, 1); layout->addLayout(actions); layout->addWidget(m_status);
    layout->addWidget(m_outputLabel); layout->addLayout(candidateRow); layout->addWidget(m_output, 1);
    layout->addLayout(resultActions); layout->addWidget(m_report); layout->addWidget(m_process);
    m_aiTitle = ui::label({}, 13, ui::Tone::Primary, content);
    m_aiHint = ui::label({}, 10, ui::Tone::Secondary, content);
    m_aiStatus = ui::label({}, 10, ui::Tone::Secondary, content); m_aiStatus->setObjectName("mojibakeAiStatus");
    m_aiAnalyze = button("mojibakeAiAnalyze");
    m_aiCancel = button("mojibakeAiCancel"); m_aiCancel->hide();
    m_aiCopy = button("mojibakeAiCopy"); m_aiCopy->setEnabled(false);
    m_aiOutput = editor("mojibakeAiOutput", true);
    m_aiExplanation = editor("mojibakeAiExplanation", true);
    m_aiOutput->hide(); m_aiExplanation->hide();
    auto* aiActions = new QHBoxLayout;
    aiActions->addWidget(m_aiAnalyze); aiActions->addWidget(m_aiCancel); aiActions->addWidget(m_aiCopy); aiActions->addStretch();
    layout->addWidget(m_aiTitle); layout->addWidget(m_aiHint); layout->addLayout(aiActions);
    layout->addWidget(m_aiStatus); layout->addWidget(m_aiOutput); layout->addWidget(m_aiExplanation);
    connect(m_aiAnalyze, &ElaPushButton::clicked, this, &MojibakePage::analyzeAi);
    connect(m_aiCancel, &ElaPushButton::clicked, m_aiService, &ai::AiService::cancel);
    connect(m_aiCopy, &ElaPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_aiResult.text); });
    m_aiProgressTimer.setInterval(1000);
    connect(&m_aiProgressTimer, &QTimer::timeout, this, &MojibakePage::showAiProgress);
    connect(m_aiService, &ai::AiService::progress, this, [this](bool thinking, int characters) {
        if (m_aiStale) return;
        m_aiThinking = thinking; m_aiCharacters = characters; showAiProgress();
    });
    connect(m_aiService, &ai::AiService::completed, this, [this](const ai::Reply& reply) {
        m_aiProgressTimer.stop();
        if (m_aiStale || m_aiSubmitted != inputText()) { updateAiControls(); return; }
        m_aiResult = reply;
        m_aiOutput->setPlainText(reply.text); m_aiOutput->setVisible(!reply.text.isEmpty());
        QStringList explanation{reply.reason};
        if (reply.selectedCandidate >= 0) explanation << tr("AI preferred local candidate %1.").arg(reply.selectedCandidate + 1);
        if (!reply.guesses.isEmpty()) explanation << tr("Contextual guesses reported by the model:") << reply.guesses;
        explanation << tr("All AI output is a suggestion. Inferred characters are not verified byte recovery.");
        m_aiExplanation->setPlainText(explanation.join('\n')); m_aiExplanation->show();
        m_aiStatus->setText(reply.text.isEmpty() ? tr("AI could not propose a reliable recovery. See its explanation.")
                                              : tr("AI suggestion received. Verify the text and any inferred characters."));
        updateAiControls();
    });
    connect(m_aiService, &ai::AiService::failed, this, [this](const QString& error) {
        m_aiProgressTimer.stop();
        if (!m_aiStale) m_aiStatus->setText(error);
        updateAiControls();
    });
    connect(m_aiService, &ai::AiService::cancelled, this, [this] {
        m_aiProgressTimer.stop();
        if (!m_aiStale) m_aiStatus->setText(tr("AI request cancelled. Local results are unchanged."));
        updateAiControls();
    });
    if (m_settings) connect(m_settings, &AppSettings::aiConfigurationChanged, this, [this] { invalidateAi(); updateAiControls(); });
    connect(m_analyze, &ElaPushButton::clicked, this, &MojibakePage::analyze);
    connect(m_clear, &ElaPushButton::clicked, this, [this]() { m_input->clear(); invalidate(); });
    connect(m_cancel, &ElaPushButton::clicked, this, [this]() { if (m_cancellation) m_cancellation->store(true); });
    connect(m_input, &ElaPlainTextEdit::textChanged, this, &MojibakePage::invalidate);
    connect(m_candidates, &QButtonGroup::idClicked, this, [this](int index) {
        m_selectedCandidate = index; m_copied = false; showResult();
    });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { showCandidate(); });
    connect(m_details, &ElaPushButton::toggled, m_process, &QWidget::setVisible);
    connect(m_copy, &ElaPushButton::clicked, this, [this]() {
        const int index = m_selectedCandidate;
        if (index < 0 || index >= m_result.candidates.size()) return;
        QApplication::clipboard()->setText(m_result.candidates[index].text);
        m_copied = true;
        showResult();
    });
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this]() {
        m_analyze->setEnabled(true); m_cancel->hide();
        if (m_stale || inputText() != m_submitted) { m_result = {}; showResult(); return; }
        m_result = m_watcher.result();
        showResult();
    });
    retranslate();
}
MojibakePage::~MojibakePage() { if (m_cancellation) m_cancellation->store(true); m_aiService->disconnect(this); m_aiService->cancel(); }
QString MojibakePage::inputText() const {
    // QTextDocument::toPlainText normalizes NBSP to ASCII spaces, destroying
    // byte evidence in common UTF-8-as-Latin-1 mojibake such as "ä½ å¥½".
    return m_input->document()->toRawText().replace(QChar::ParagraphSeparator, '\n')
        .replace(QChar::LineSeparator, '\n');
}
void MojibakePage::invalidate() {
    invalidateAi();
    m_stale = true; m_copied = false;
    if (m_cancellation) m_cancellation->store(true);
    m_result = {}; m_output->clear(); m_process->clear();
    m_report->setReport({});
    m_selectedCandidate = -1;
    m_candidates->setExclusive(false);
    for (auto* b : m_candidateButtons) { b->setChecked(false); b->setEnabled(false); }
    m_candidates->setExclusive(true);
    m_copy->setEnabled(false); m_details->setEnabled(false);
    showCandidate();
    updateAiControls();
    if (!m_watcher.isRunning()) showResult();
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
    const auto text = m_submitted;
    const auto cancel = m_cancellation;
    // No QWidget or page pointer is captured by the worker.
    m_watcher.setFuture(QtConcurrent::run([text, cancel]() { return MojibakeAnalyzer::analyze(text, cancel); }));
}
void MojibakePage::showResult() {
    m_selectedCandidate = m_result.candidates.isEmpty() ? -1 : qBound(0, m_selectedCandidate, int(m_result.candidates.size()) - 1);
    m_report->setReport(m_watcher.isRunning() ? DiagnosticReport{} : DiagnosticReport::text(m_result, m_selectedCandidate));
    m_candidates->setExclusive(false);
    for (int i = 0; i < 5; ++i) {
        auto* b = m_candidateButtons[i];
        const bool available = i < m_result.candidates.size();
        b->setEnabled(available);
        b->setChecked(available && i == m_selectedCandidate);
        b->setText(available ? tr("Candidate %1\n%2 / 100").arg(i + 1).arg(m_result.candidates[i].score, 0, 'f', 1)
                             : tr("Candidate %1\nUnavailable").arg(i + 1));
        b->setToolTip(available && m_result.candidates[i].path.isEmpty() ? tr("Original text (unchanged)") : QString());
    }
    m_candidates->setExclusive(true);
    m_details->setEnabled(!m_result.candidates.isEmpty());
    ui::setTone(m_status, ui::Tone::Secondary);
    if (m_watcher.isRunning()) m_status->setText(tr("Analyzing locally…"));
    else if (m_result.tooLarge) m_status->setText(tr("Input exceeds the 1 MiB limit. Analyze a smaller section."));
    else if (m_result.cancelled) m_status->setText(tr("Analysis cancelled."));
    else if (m_result.candidates.isEmpty()) m_status->setText(tr("Paste text, then analyze. Scores rank possibilities; they are not probabilities."));
    else if (m_result.possibleLoss) m_status->setText(tr("Possible missing information: replacement characters or repeated question marks. Recovery is uncertain."));
    else if (!m_result.likelyMojibake) m_status->setText(tr("No reliable improvement found. The original text is retained; alternatives may be incorrect."));
    else if (m_result.confidence == Confidence::High) m_status->setText(tr("High confidence candidate. Check the meaning before using it."));
    else m_status->setText(tr("Possible recovery. Several encodings may explain the same bytes; compare the candidates."));
    if (m_copied) m_status->setText(m_status->text() + " " + tr("Copied to clipboard."));
    showCandidate();
    updateAiControls();
}
void MojibakePage::showCandidate() {
    for (auto* b : m_candidateButtons) {
        const bool selected = b->isChecked();
        b->setLightDefaultColor(eTheme->getThemeColor(ElaThemeType::Light, selected ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase));
        b->setLightHoverColor(eTheme->getThemeColor(ElaThemeType::Light, selected ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover));
        b->setLightPressColor(eTheme->getThemeColor(ElaThemeType::Light, selected ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress));
        b->setLightTextColor(eTheme->getThemeColor(ElaThemeType::Light, selected ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText));
        b->setDarkDefaultColor(eTheme->getThemeColor(ElaThemeType::Dark, selected ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase));
        b->setDarkHoverColor(eTheme->getThemeColor(ElaThemeType::Dark, selected ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover));
        b->setDarkPressColor(eTheme->getThemeColor(ElaThemeType::Dark, selected ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress));
        b->setDarkTextColor(eTheme->getThemeColor(ElaThemeType::Dark, selected ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText));
        b->update();
    }
    const int index = m_selectedCandidate;
    if (index < 0 || index >= m_result.candidates.size()) { m_output->clear(); m_copy->setEnabled(false); return; }
    const auto& candidate = m_result.candidates[index];
    m_output->setPlainText(candidate.text);
    m_copy->setEnabled(!candidate.text.isEmpty());
    QStringList path;
    path << tr("Quality score: %1 / 100 (not a probability).").arg(candidate.score, 0, 'f', 1);
    path << tr("Ranking uses anomaly density, conversion count, a preference for UTF-8 decoding, and penalties for incomplete recovery.");
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
    path << tr("Search depth: at most 2; examined %1 unique candidates.").arg(m_result.generated);
    if (m_result.searchLimited) path << tr("The bounded search was pruned; other possibilities may exist.");
    path << tr("A reversible encoding path does not prove that the meaning is correct.");
    m_process->setPlainText(path.join('\n'));
}
void MojibakePage::retranslate() {
    m_title->setText(tr("Mojibake Repair"));
    m_textMode->setText(tr("Pasted text")); m_fileMode->setText(tr("Text file"));
    m_nameMode->setText(tr("File and folder names"));
    m_clipboardMode->setText(tr("Clipboard"));
    m_description->setText(tr("Local analysis stays offline. AI assistance sends text only when you explicitly request it."));
    m_inputLabel->setText(tr("Original text")); m_outputLabel->setText(tr("Recovery candidates"));
    m_analyze->setText(tr("Analyze")); m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear"));
    m_copy->setText(tr("Copy result")); m_details->setText(tr("Recovery process"));
    m_input->setPlaceholderText(tr("Paste garbled text here…"));
    m_aiTitle->setText(tr("AI suggestion (semantic inference)"));
    m_aiAnalyze->setText(tr("AI deep analysis")); m_aiCancel->setText(tr("Cancel AI")); m_aiCopy->setText(tr("Copy AI suggestion"));
    showResult();
}
void MojibakePage::invalidateAi() {
    m_aiProgressTimer.stop();
    m_aiStale = true; m_aiService->cancel(); m_aiResult = {};
    m_aiOutput->clear(); m_aiExplanation->clear(); m_aiStatus->clear();
    m_aiOutput->hide(); m_aiExplanation->hide(); m_aiCopy->setEnabled(false);
}
void MojibakePage::updateAiControls() {
    const auto config = m_settings ? m_settings->aiConfiguration() : ai::Configuration{};
    const bool configured = m_settings && ai::AiService::validate(config).isEmpty();
    const bool busy = m_aiService->isBusy();
    m_aiAnalyze->setEnabled(configured && !busy && !m_watcher.isRunning() && !m_result.candidates.isEmpty());
    m_aiCancel->setVisible(busy);
    m_aiCopy->setEnabled(!m_aiResult.text.isEmpty() && !busy);
    m_aiHint->setText(configured ? tr("Clicking AI analysis sends the original text and local candidates to %1 using model %2. Suggestions may contain inferred characters. Run local analysis first.").arg(config.baseUrl, config.model)
                               : tr("Configure the AI API address and model in Settings first. Local analysis does not upload text."));
}
void MojibakePage::analyzeAi() {
    if (!m_settings || m_aiService->isBusy() || m_watcher.isRunning() || m_result.candidates.isEmpty()) return;
    invalidateAi(); m_aiStale = false; m_aiSubmitted = inputText();
    QJsonArray candidates;
    for (int i = 0; i < m_result.candidates.size(); ++i) {
        const auto& candidate = m_result.candidates[i];
        QJsonArray path;
        for (const auto& step : candidate.path) path.append(QJsonObject{
            {"encode_as", EncodingConverter::name(step.encodeAs)}, {"decode_as", EncodingConverter::name(step.decodeAs)},
            {"segmented", step.segmented}, {"partial", step.partial}});
        candidates.append(QJsonObject{{"index", i + 1}, {"text", candidate.text}, {"quality_score", candidate.score},
                                      {"inverse_verified", candidate.reversible}, {"path", path}});
    }
    const QJsonObject context{{"original_text", m_aiSubmitted}, {"candidates", candidates}, {"possible_loss", m_result.possibleLoss}};
    m_aiCharacters = 0; m_aiThinking = false; m_aiElapsed.start();
    m_aiStatus->setText(tr("Waiting for AI response…"));
    m_aiService->analyze(m_settings->aiConfiguration(), context, m_settings->locale());
    if (m_aiService->isBusy()) { m_aiProgressTimer.start(); showAiProgress(); }
    updateAiControls();
}
void MojibakePage::showAiProgress() {
    if (m_aiStale || !m_aiService->isBusy()) return;
    const auto seconds = m_aiElapsed.elapsed() / 1000;
    if (!m_aiCharacters) m_aiStatus->setText(tr("Waiting for the service to start responding… %1 s elapsed (90 s limit).").arg(seconds));
    else if (m_aiThinking) m_aiStatus->setText(tr("The model is thinking… %1 s elapsed, %2 reasoning characters received.").arg(seconds).arg(m_aiCharacters));
    else m_aiStatus->setText(tr("Receiving AI reply… %1 s elapsed, %2 output characters received. The result will be validated when complete.").arg(seconds).arg(m_aiCharacters));
}
void MojibakePage::changeEvent(QEvent* event) {
    ToolPage::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
