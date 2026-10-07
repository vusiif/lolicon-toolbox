#include "TextFileRecoveryPage.h"
#include "DiagnosticReportWidget.h"
#include "UiTheme.h"
#include <ElaScrollArea.h>
#include <ElaPushButton.h>
#include <ElaPlainTextEdit.h>
#include <ElaComboBox.h>
#include <ElaCheckBox.h>
#include <ElaTheme.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QSignalBlocker>
#include <QtConcurrentRun>
using namespace mojibake;
namespace { constexpr qsizetype PreviewCharacters = 32768; }
TextFileRecoveryPage::TextFileRecoveryPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content = new QWidget(scroll); content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false); scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(32, 28, 32, 28); layout->setSpacing(12);
    m_title = ui::label({}, 18, ui::Tone::Primary, content);
    m_description = ui::label({}, 10, ui::Tone::Secondary, content);
    m_pathLabel = ui::label({}, 10, ui::Tone::Secondary, content); m_pathLabel->setTextFormat(Qt::PlainText);
    m_status = ui::label({}, 10, ui::Tone::Secondary, content); m_status->setObjectName("fileRecoveryStatus"); m_status->setTextFormat(Qt::PlainText);
    auto button = [content](const char* name) { auto* b = new ElaPushButton(content); b->setObjectName(name); return b; };
    m_open = button("fileRecoveryOpen"); ui::primaryButton(m_open);
    m_analyze = button("fileRecoveryAnalyze"); m_cancel = button("fileRecoveryCancel");
    m_save = button("fileRecoverySave");
    auto* actions = new QHBoxLayout; actions->addWidget(m_open); actions->addWidget(m_analyze); actions->addWidget(m_cancel); actions->addStretch();
    m_sourceLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_source = new ElaComboBox(content); m_source->setObjectName("fileRecoverySource"); m_source->addItem({}, -1);
    m_outputLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_output = new ElaComboBox(content); m_output->setObjectName("fileRecoveryOutputEncoding");
    for (auto e : EncodingConverter::supported()) {
        m_source->addItem(EncodingConverter::name(e), int(e)); m_output->addItem(EncodingConverter::name(e), int(e));
    }
    m_newlineLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_newlines = new ElaComboBox(content); m_newlines->setObjectName("fileRecoveryNewlines");
    for (int i = 0; i < 3; ++i) m_newlines->addItem({}, i);
    m_bom = new ElaCheckBox(content); m_bom->setObjectName("fileRecoveryOutputBom");
    auto* sourceRow = new QHBoxLayout; sourceRow->addWidget(m_sourceLabel); sourceRow->addWidget(m_source); sourceRow->addStretch();
    m_candidates = new QButtonGroup(this); m_candidates->setExclusive(true);
    auto* candidatesRow = new QHBoxLayout; candidatesRow->setSpacing(8);
    for (int i = 0; i < 5; ++i) {
        auto* b = button(qPrintable(QString("fileRecoveryCandidate%1").arg(i + 1)));
        b->setCheckable(true); b->setFixedHeight(60); b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto font = b->font(); font.setPixelSize(12); b->setFont(font);
        m_buttons[i] = b; m_candidates->addButton(b, i); candidatesRow->addWidget(b, 1);
    }
    m_preview = new ElaPlainTextEdit(content); m_preview->setObjectName("fileRecoveryPreview"); m_preview->setReadOnly(true); m_preview->setMinimumHeight(160);
    m_details = new ElaPlainTextEdit(content); m_details->setObjectName("fileRecoveryDetails"); m_details->setReadOnly(true); m_details->setMinimumHeight(130);
    m_report = new DiagnosticReportWidget(content); m_report->setObjectName("fileDiagnosticReport");
    auto* outputForm = new QFormLayout; outputForm->addRow(m_outputLabel, m_output); outputForm->addRow(m_newlineLabel, m_newlines); outputForm->addRow(m_bom);
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addLayout(actions); layout->addWidget(m_pathLabel);
    layout->addLayout(sourceRow); layout->addWidget(m_status); layout->addLayout(candidatesRow);
    layout->addWidget(m_preview); layout->addWidget(m_report); layout->addWidget(m_details); layout->addLayout(outputForm); layout->addWidget(m_save, 0, Qt::AlignLeft); layout->addStretch();
    connect(m_open, &ElaPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open a text file"), {}, tr("Text files (*.txt *.csv *.log *.json *.ini *.xml *.yaml *.yml *.cpp *.h *.py *.md);;All files (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(m_analyze, &ElaPushButton::clicked, this, [this] { openFile(m_path); });
    connect(m_cancel, &ElaPushButton::clicked, this, [this] { if (m_cancelToken) m_cancelToken->store(true); });
    connect(m_source, &QComboBox::currentIndexChanged, this, [this] { if (!m_path.isEmpty()) openFile(m_path); });
    connect(m_output, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_candidates, &QButtonGroup::idClicked, this, [this](int id) { m_selected = id; m_savedPath.clear(); m_saveError.clear(); refresh(); });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { showCandidate(); });
    connect(m_save, &ElaPushButton::clicked, this, [this] {
        const QFileInfo source(m_path);
        const auto suggested = source.dir().filePath(source.completeBaseName() + ".repaired" + (source.suffix().isEmpty() ? "" : "." + source.suffix()));
        QFileDialog dialog(this, tr("Create a repaired copy"), suggested);
        dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setOption(QFileDialog::DontConfirmOverwrite); // Backend always rejects existing paths.
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) exportCopy(dialog.selectedFiles().first());
    });
    connect(&m_analysis, &QFutureWatcher<FileResult>::finished, this, [this] {
        m_result = m_analysis.result(); m_selected = m_result.candidates.isEmpty() ? -1 : 0;
        if (m_resetOutput) {
            const QSignalBlocker block(m_output); m_output->setCurrentIndex(m_output->findData(int(Encoding::Utf8)));
            m_newlines->setCurrentIndex(0); m_bom->setChecked(m_result.bomEncoding.has_value());
        }
        refresh();
    });
    connect(&m_export, &QFutureWatcher<SaveResult>::finished, this, [this] {
        const auto result = m_export.result(); m_saveError = result.error;
        m_savedPath = result.success ? m_pendingDestination : QString(); refresh();
    });
    retranslate();
}
TextFileRecoveryPage::~TextFileRecoveryPage() { if (m_cancelToken) m_cancelToken->store(true); }
std::optional<Encoding> TextFileRecoveryPage::forcedEncoding() const {
    return m_source->currentData().toInt() < 0 ? std::nullopt : std::optional{Encoding(m_source->currentData().toInt())};
}
Encoding TextFileRecoveryPage::outputEncoding() const { return Encoding(m_output->currentData().toInt()); }
void TextFileRecoveryPage::openFile(const QString& path) {
    if (path.isEmpty() || m_analysis.isRunning() || m_export.isRunning()) return;
    m_resetOutput = m_path != path || m_result.bytes.isEmpty();
    m_path = path; m_result = {}; m_selected = -1; m_savedPath.clear(); m_saveError.clear();
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancelToken; const auto forced = forcedEncoding();
    m_analysis.setFuture(QtConcurrent::run([path, forced, cancel] { return ByteEncodingAnalyzer::load(path, forced, cancel); }));
    refresh();
}
void TextFileRecoveryPage::exportCopy(const QString& destination) {
    if (destination.isEmpty() || m_analysis.isRunning() || m_export.isRunning() || m_selected < 0) return;
    const auto snapshot = m_result; const auto selected = m_selected; const auto encoding = outputEncoding();
    const bool bom = m_bom->isEnabled() && m_bom->isChecked(); const auto newlines = Newlines(m_newlines->currentData().toInt());
    m_pendingDestination = destination; m_savedPath.clear(); m_saveError.clear();
    m_export.setFuture(QtConcurrent::run([snapshot, selected, destination, encoding, bom, newlines] {
        return RecoveryOperation::saveCopy(snapshot, selected, destination, encoding, bom, newlines);
    }));
    refresh();
}
void TextFileRecoveryPage::refresh() {
    const bool busy = m_analysis.isRunning() || m_export.isRunning();
    m_report->setReport(busy ? DiagnosticReport{} : DiagnosticReport::file(m_result, m_selected));
    m_open->setEnabled(!busy); m_source->setEnabled(!busy); m_analyze->setEnabled(!busy && !m_path.isEmpty());
    m_save->setEnabled(!busy && m_selected >= 0); m_output->setEnabled(!busy); m_newlines->setEnabled(!busy);
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
    else m_status->setText(tr("Compare the candidates before exporting. No existing file will be overwritten."));
    m_candidates->setExclusive(false);
    for (int i = 0; i < 5; ++i) {
        const bool available = i < m_result.candidates.size(); auto* b = m_buttons[i];
        b->setEnabled(available && !busy); b->setChecked(available && i == m_selected);
        b->setText(available ? QString("%1\n%2 / 100").arg(EncodingConverter::name(m_result.candidates[i].encoding)).arg(m_result.candidates[i].score, 0, 'f', 1)
                            : tr("Candidate %1\nUnavailable").arg(i + 1));
    }
    m_candidates->setExclusive(true); showCandidate();
}
void TextFileRecoveryPage::showCandidate() {
    for (auto* b : m_buttons) {
        const bool selected = b->isChecked();
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
    if (m_selected < 0 || m_selected >= m_result.candidates.size()) { m_preview->clear(); m_details->clear(); return; }
    const auto& c = m_result.candidates[m_selected];
    auto length = std::min<qsizetype>(c.text.size(), PreviewCharacters);
    if (length < c.text.size() && length && c.text[length - 1].isHighSurrogate()) --length;
    m_preview->setPlainText(c.text.left(length));
    QStringList aliases; for (auto e : c.aliases) aliases << EncodingConverter::name(e);
    const auto crlf = c.text.count("\r\n"), lf = c.text.count('\n') - crlf, cr = c.text.count('\r') - crlf;
    QStringList details{
        tr("Source bytes: %1; decoded characters: %2.").arg(m_result.bytes.size()).arg(c.text.size()),
        tr("Source encoding candidates: %1").arg(aliases.join(", ")),
        tr("BOM: %1").arg(m_result.bomEncoding ? EncodingConverter::name(*m_result.bomEncoding) : tr("None")),
        tr("Newlines: CRLF %1, LF %2, CR %3.").arg(crlf).arg(lf).arg(cr),
        tr("Exact inverse verified: %1").arg(c.reversible ? tr("Yes") : tr("No; partial recovery"))};
    if (m_result.preferredEncoding) details << tr("Byte evidence prefers %1. This does not prove the original meaning.").arg(EncodingConverter::name(*m_result.preferredEncoding));
    for (const auto& step : c.repairPath) details << tr("Text repair: encode as %1, then decode as %2.").arg(EncodingConverter::name(step.encodeAs), EncodingConverter::name(step.decodeAs));
    if (c.text.contains(QChar(0xfffd)) || !c.reversible) details << tr("Possible missing information. Unknown characters cannot be uniquely restored.");
    if (m_result.inverseRepairSkipped) details << tr("For large text, only byte encoding detection was performed; further mojibake search is limited to 1 MiB.");
    if (c.text.size() > length) details << tr("Preview shows the first %1 characters. Export includes the complete candidate.").arg(length);
    details << tr("Scores rank possibilities, not probabilities. File contents are not sent to AI.");
    m_details->setPlainText(details.join('\n'));
}
void TextFileRecoveryPage::retranslate() {
    m_title->setText(tr("Text file encoding diagnosis"));
    m_description->setText(tr("Analyze original bytes and preview decoding or repair candidates. Save a new copy; the source stays unchanged."));
    m_open->setText(tr("Choose file")); m_analyze->setText(tr("Analyze again")); m_cancel->setText(tr("Cancel")); m_save->setText(tr("Save repaired copy…"));
    m_sourceLabel->setText(tr("Source encoding")); m_source->setItemText(0, tr("Automatic comparison"));
    m_outputLabel->setText(tr("Output encoding")); m_newlineLabel->setText(tr("Output newlines"));
    m_newlines->setItemText(0, tr("Preserve original")); m_newlines->setItemText(1, "LF"); m_newlines->setItemText(2, "CRLF");
    m_bom->setText(tr("Write BOM (defaults to source BOM presence)")); refresh();
}
void TextFileRecoveryPage::changeEvent(QEvent* event) { QWidget::changeEvent(event); if (event->type() == QEvent::LanguageChange) retranslate(); }
