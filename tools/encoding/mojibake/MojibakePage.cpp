#include "MojibakePage.h"
#include "MojibakeAnalyzer.h"
#include "UiTheme.h"
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaComboBox.h>
#include <ElaScrollArea.h>
#include <QtConcurrentRun>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QClipboard>
#include <QApplication>
#include <QSignalBlocker>
#include <QTextDocument>
using namespace mojibake;
MojibakePage::MojibakePage(QWidget* parent) : ToolPage(parent) {
    setObjectName("mojibakePage");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);
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
    m_output = editor("mojibakeOutput", true);
    m_process = editor("mojibakeProcess", true);
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
    m_candidates = new ElaComboBox(content);
    m_candidates->setObjectName("mojibakeCandidates");
    m_candidates->setEnabled(false);
    auto* actions = new QHBoxLayout;
    actions->addWidget(m_analyze); actions->addWidget(m_cancel); actions->addWidget(m_clear); actions->addStretch();
    auto* resultActions = new QHBoxLayout;
    resultActions->addWidget(m_copy); resultActions->addWidget(m_details); resultActions->addStretch();
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addWidget(m_inputLabel);
    layout->addWidget(m_input, 1); layout->addLayout(actions); layout->addWidget(m_status);
    layout->addWidget(m_outputLabel); layout->addWidget(m_candidates); layout->addWidget(m_output, 1);
    layout->addLayout(resultActions); layout->addWidget(m_process);
    connect(m_analyze, &ElaPushButton::clicked, this, &MojibakePage::analyze);
    connect(m_clear, &ElaPushButton::clicked, this, [this]() { m_input->clear(); invalidate(); });
    connect(m_cancel, &ElaPushButton::clicked, this, [this]() { if (m_cancellation) m_cancellation->store(true); });
    connect(m_input, &ElaPlainTextEdit::textChanged, this, &MojibakePage::invalidate);
    connect(m_candidates, &ElaComboBox::currentIndexChanged, this, &MojibakePage::showCandidate);
    connect(m_details, &ElaPushButton::toggled, m_process, &QWidget::setVisible);
    connect(m_copy, &ElaPushButton::clicked, this, [this]() {
        const int index = m_candidates->currentIndex();
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
MojibakePage::~MojibakePage() { if (m_cancellation) m_cancellation->store(true); }
QString MojibakePage::inputText() const {
    // QTextDocument::toPlainText normalizes NBSP to ASCII spaces, destroying
    // byte evidence in common UTF-8-as-Latin-1 mojibake such as "ä½ å¥½".
    return m_input->document()->toRawText().replace(QChar::ParagraphSeparator, '\n')
        .replace(QChar::LineSeparator, '\n');
}
void MojibakePage::invalidate() {
    m_stale = true; m_copied = false;
    if (m_cancellation) m_cancellation->store(true);
    m_result = {}; m_output->clear(); m_process->clear();
    { QSignalBlocker blocker(m_candidates); m_candidates->clear(); }
    m_candidates->setEnabled(false); m_copy->setEnabled(false); m_details->setEnabled(false);
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
    const int oldIndex = m_candidates->currentIndex();
    {
        QSignalBlocker blocker(m_candidates);
        m_candidates->clear();
        for (int i = 0; i < m_result.candidates.size(); ++i) {
            const auto& c = m_result.candidates[i];
            const auto label = c.path.isEmpty() ? tr("Original text (unchanged)") : tr("Candidate %1 — score %2").arg(i + 1).arg(c.score, 0, 'f', 1);
            m_candidates->addItem(label);
        }
        if (m_candidates->count()) m_candidates->setCurrentIndex(qBound(0, oldIndex, m_candidates->count() - 1));
    }
    m_candidates->setEnabled(!m_result.candidates.isEmpty());
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
}
void MojibakePage::showCandidate() {
    const int index = m_candidates->currentIndex();
    if (index < 0 || index >= m_result.candidates.size()) { m_output->clear(); m_copy->setEnabled(false); return; }
    const auto& candidate = m_result.candidates[index];
    m_output->setPlainText(candidate.text);
    m_copy->setEnabled(!candidate.text.isEmpty());
    QStringList path;
    if (candidate.path.isEmpty()) path << tr("Original text: no conversion applied.");
    for (int i = 0; i < candidate.path.size(); ++i) {
        const auto& step = candidate.path[i];
        path << tr("Step %1: encode as %2, then decode as %3.").arg(i + 1)
            .arg(EncodingConverter::name(step.encodeAs), EncodingConverter::name(step.decodeAs));
        if (step.segmented) path << tr("Only representable text segments were changed; other text was preserved.");
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
    m_description->setText(tr("Analyze encoding mistakes locally and compare recovery candidates. Your text stays on this computer."));
    m_inputLabel->setText(tr("Original text")); m_outputLabel->setText(tr("Recovery candidates"));
    m_analyze->setText(tr("Analyze")); m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear"));
    m_copy->setText(tr("Copy result")); m_details->setText(tr("Recovery process"));
    m_input->setPlaceholderText(tr("Paste garbled text here…"));
    showResult();
}
void MojibakePage::changeEvent(QEvent* event) {
    ToolPage::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
