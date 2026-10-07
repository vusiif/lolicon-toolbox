#include "TextStylePage.h"
#include "services/AppSettings.h"
#include "UiTheme.h"
#include <ElaScrollArea.h>
#include <ElaPlainTextEdit.h>
#include <ElaLineEdit.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaIconButton.h>
#include <QValidator>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QClipboard>
#include <QApplication>
#include <QTextDocument>
#include <QRandomGenerator>
#include <ElaTheme.h>
#include <QPalette>
#include <QEvent>
#include <QResizeEvent>
#include <QtConcurrentRun>
using namespace textstyle;
namespace {
class SeedValidator final : public QValidator {
public:
    explicit SeedValidator(QObject* parent) : QValidator(parent) {}
    State validate(QString& input, int&) const override {
        if (input.isEmpty()) return Intermediate;
        UserSeed parsed;
        if (!Engine::userSeed(input, parsed)) return Invalid;
        // Lowercase ASCII only; preserve decimal leading zeroes while editing.
        input = input.toLower(); return Acceptable;
    }
};
}
TextStylePage::TextStylePage(QWidget* parent, AppSettings* settings) : ToolPage(parent), m_settings(settings), m_loaded(Loader::builtin()), m_runner(Engine::transform) {
    setObjectName("textStylePage");
    m_clipboardWriter = [](const QString& text) { auto* clipboard = QApplication::clipboard(); clipboard->setText(text); return clipboard->ownsClipboard(); };
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content = new QWidget(scroll); content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false); scroll->setWidget(content);
    auto backdrop = [this, content, scroll] {
        auto colors = qApp->palette(); const auto background = eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::WindowBase);
        colors.setColor(QPalette::Window, background); colors.setColor(QPalette::Base, background);
        setPalette(colors); setAutoFillBackground(true); content->setPalette(colors); content->setAutoFillBackground(true);
        scroll->viewport()->setPalette(colors); scroll->viewport()->setAutoFillBackground(true);
    };
    connect(eTheme, &ElaTheme::themeModeChanged, this, backdrop); backdrop();
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(32, 28, 32, 28); layout->setSpacing(12);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_title = ui::label({}, 18, ui::Tone::Primary, content); m_description = ui::label({}, 10, ui::Tone::Secondary, content);
    m_styleLabel = ui::label({}, 10, ui::Tone::Secondary, content); m_presetLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_inputLabel = ui::label({}, 10, ui::Tone::Secondary, content); m_outputLabel = ui::label({}, 10, ui::Tone::Secondary, content);
    m_status = ui::label({}, 10, ui::Tone::Secondary, content); m_status->setTextFormat(Qt::PlainText); m_status->setObjectName("textStyleStatus");
    m_styles = new ElaComboBox(content); m_styles->setObjectName("textStyleStyle"); if (m_loaded.style) m_styles->addItem({}, m_loaded.style->id);
    m_presets = new ElaComboBox(content); m_presets->setObjectName("textStylePreset");
    for (const auto* preset : {"light", "medium", "heavy"}) m_presets->addItem(QString(), QString::fromLatin1(preset));
    m_presets->setCurrentIndex(m_presets->findData(settings ? settings->textStylePreset() : "medium"));
    m_styles->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_presets->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* options = new QHBoxLayout;
    options->addWidget(m_styleLabel); options->addWidget(m_styles, 2);
    options->addWidget(m_presetLabel); options->addWidget(m_presets, 1);
    auto* form = new QFormLayout;
    auto editor = [content](const char* name, bool readOnly) { auto* w = new ElaPlainTextEdit(content); w->setObjectName(name); w->setReadOnly(readOnly); w->setMinimumHeight(160); return w; };
    m_input = editor("textStyleInput", false); m_output = editor("textStyleOutput", true);
    auto* columns = new QHBoxLayout; m_columns = columns;
    auto* left = new QVBoxLayout; left->addWidget(m_inputLabel); left->addWidget(m_input);
    auto* right = new QVBoxLayout; right->addWidget(m_outputLabel); right->addWidget(m_output);
    columns->addLayout(left, 1); columns->addLayout(right, 1);
    auto button = [content](const char* name) { auto* w = new ElaPushButton(content); w->setObjectName(name); return w; };
    m_convert = button("textStyleConvert"); ui::primaryButton(m_convert); m_randomConvert = button("textStyleRandomConvert"); m_cancel = button("textStyleCancel"); m_clear = button("textStyleClear"); m_copy = button("textStyleCopy");
    auto* actions = new QHBoxLayout; actions->addWidget(m_convert); actions->addWidget(m_randomConvert); actions->addWidget(m_cancel); actions->addWidget(m_clear); actions->addStretch();
    auto* outputActions = new QHBoxLayout; outputActions->addWidget(m_copy); outputActions->addStretch();
    left->addLayout(actions); right->addLayout(outputActions);
    m_seedLabel = ui::label({}, 10, ui::Tone::Secondary, content); m_seed = new ElaLineEdit(content); m_seed->setObjectName("textStyleSeed"); m_seed->setMaxLength(64);
    m_seed->setValidator(new SeedValidator(m_seed));
    m_seed->setText(QString::number(QRandomGenerator::system()->generate64()));
    m_randomSeed = new ElaIconButton(ElaIconType::Dice, 20, 36, 36, content); m_randomSeed->setObjectName("textStyleRandomSeed");
    m_randomSeed->setBorderRadius(6);
    m_copySeed = button("textStyleCopySeed");
    auto* seedRow = new QHBoxLayout; seedRow->addWidget(m_seed, 1); seedRow->addWidget(m_randomSeed); seedRow->addWidget(m_copySeed);
    form->addRow(m_seedLabel, seedRow);
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addLayout(options); layout->addLayout(form); layout->addLayout(columns); layout->addWidget(m_status);
    layout->addStretch();
    connect(m_input, &ElaPlainTextEdit::textChanged, this, &TextStylePage::invalidate);
    connect(m_seed, &QLineEdit::textChanged, this, &TextStylePage::invalidate);
    connect(m_styles, &QComboBox::currentIndexChanged, this, &TextStylePage::invalidate);
    connect(m_presets, &QComboBox::currentIndexChanged, this, [this] { invalidate(); savePreferences(); });
    connect(m_convert, &ElaPushButton::clicked, this, &TextStylePage::run);
    connect(m_randomConvert, &ElaPushButton::clicked, this, [this] {
        if (m_worker.isRunning() || inputText().isEmpty() || !m_loaded.style) return;
        randomizeSeed(); run();
    });
    connect(m_randomSeed, &ElaIconButton::clicked, this, &TextStylePage::randomizeSeed);
    connect(m_cancel, &ElaPushButton::clicked, this, [this] { invalidate(); m_error = Error::Cancelled; refresh(); });
    connect(m_clear, &ElaPushButton::clicked, this, [this] { m_input->clear(); invalidate(); });
    connect(m_copy, &ElaPushButton::clicked, this, [this] { if (m_success) { m_copied = m_clipboardWriter(m_result.output); m_clipboardFailed = !m_copied; refresh(); } });
    connect(m_copySeed, &ElaPushButton::clicked, this, [this] { UserSeed seed; if (Engine::userSeed(m_seed->text(), seed)) { m_copied = m_clipboardWriter(seed.text); m_clipboardFailed = !m_copied; refresh(); } });
    connect(&m_worker, &QFutureWatcher<Result>::finished, this, [this] {
        if (m_submittedRevision != m_revision) { refresh(); return; }
        auto result = m_worker.result(); m_error = result.error; m_success = result.success();
        if (m_success) { m_result = std::move(result); const QSignalBlocker block(m_seed); m_seed->setText(m_result.metadata["seed_text"].toString()); m_output->setPlainText(m_result.output); }
        refresh();
    });
    retranslate();
}
TextStylePage::~TextStylePage() { if (m_cancelToken) m_cancelToken->store(true); }
QString TextStylePage::inputText() const { return m_input->document()->toRawText(); }
void TextStylePage::savePreferences() { if (m_settings) m_settings->setTextStylePreset(m_presets->currentData().toString()); }
void TextStylePage::invalidate() {
    ++m_revision; if (m_cancelToken) m_cancelToken->store(true); m_success = false; m_result = {}; m_error = Error::None; m_copied = false; m_clipboardFailed = false; m_output->clear(); refresh();
}
void TextStylePage::randomizeSeed() {
    UserSeed current; const bool valid = Engine::userSeed(m_seed->text(), current);
    auto candidate = QRandomGenerator::system()->generate64();
    if (valid && candidate == current.value) ++candidate;
    m_seed->setText(QString::number(candidate));
}
void TextStylePage::run() {
    if (m_worker.isRunning() || inputText().isEmpty()) return;
    UserSeed seed;
    if (!Engine::userSeed(m_seed->text(), seed)) { invalidate(); m_error = Error::InvalidSeed; refresh(); return; }
    if (!m_loaded.style) { m_error = m_loaded.error; refresh(); return; }
    invalidate(); m_submittedRevision = m_revision; m_cancelToken = std::make_shared<std::atomic_bool>(false);
    const auto input = inputText(), preset = m_presets->currentData().toString(); const auto style = m_loaded.style; const auto token = m_cancelToken; const auto runner = m_runner;
    m_worker.setFuture(QtConcurrent::run([input, preset, style, seed, token, runner] {
        auto result = runner(input, style, preset, seed.value, Control{token});
        if (result.success()) {
            result.metadata["reproduction_schema_version"] = 2;
            result.metadata["seed_text"] = seed.text; result.metadata["seed_mapping_id"] = seed.mapping;
        }
        return result;
    })); refresh();
}
void TextStylePage::refresh() {
    const bool running = m_worker.isRunning(); const auto input = inputText(); UserSeed seed;
    const bool validSeed = Engine::userSeed(m_seed->text(), seed);
    m_convert->setEnabled(!running && !input.isEmpty() && validSeed && bool(m_loaded.style)); m_cancel->setVisible(running);
    m_randomConvert->setEnabled(!running && !input.isEmpty() && bool(m_loaded.style));
    m_copy->setEnabled(m_success && !running); m_copySeed->setEnabled(validSeed);
    if (!m_loaded.style) m_status->setText(tr("Style unavailable: %1").arg(m_loaded.detail));
    else if (running) m_status->setText(tr("Converting locally… Editing input or options cancels this snapshot."));
    else if (!validSeed || m_error == Error::InvalidSeed) m_status->setText(tr("Enter a seed or use the dice button."));
    else if (m_error != Error::None) {
        const auto name = errorName(m_error);
        if (m_error == Error::Cancelled) m_status->setText(tr("Conversion cancelled."));
        else if (m_error == Error::InputTooLarge) m_status->setText(tr("Input exceeds 256 KiB after newline normalization or the UTF-16 limit."));
        else if (m_error == Error::OutputTooLarge) m_status->setText(tr("Output exceeds 2 MiB. Use a shorter input or a lighter preset."));
        else if (m_error == Error::InvalidUnicodeInput) m_status->setText(tr("Input contains invalid Unicode surrogate sequences."));
        else if (m_error == Error::Timeout) m_status->setText(tr("Conversion exceeded the 5-second budget. Use a smaller input."));
        else m_status->setText(tr("Conversion failed (%1). Check the style and options.").arg(name));
    } else if (m_success) {
        const auto& s = m_result.stats;
        m_status->setText(tr("Matched %1 · replaced %2 · decorated lines %3 · output %4 bytes.").arg(s.matched).arg(s.replaced).arg(s.decorated).arg(s.outputBytes) +
            (s.matched == 0 && s.decorated == 0 ? " " + tr("No rules matched; only newline normalization was applied.") : QString()) + (m_copied ? " " + tr("Copied.") : QString()));
    } else m_status->setText(input.isEmpty() ? tr("Enter text to convert. Text and seeds are not saved between sessions.") : tr("Ready. Convert uses the displayed seed. Use the dice button to change it."));
    if (m_clipboardFailed) m_status->setText(m_status->text() + " " + tr("Clipboard ownership was not obtained. Another application may be using it; retry copying."));
}
void TextStylePage::retranslate() {
    m_title->setText(tr("Text Style Converter")); m_description->setText(tr("Local, reproducible text styling. Rules are data-driven; unknown characters stay intact and newlines become LF. No AI or reverse restoration."));
    m_styleLabel->setText(tr("Style")); m_presetLabel->setText(tr("Intensity")); m_inputLabel->setText(tr("Original text")); m_outputLabel->setText(tr("Styled text"));
    const auto locale = m_settings ? m_settings->locale() : "en";
    if (m_loaded.style) { QSignalBlocker block(m_styles); m_styles->setItemText(0, m_loaded.style->names.value(locale).toString(m_loaded.style->names["en"].toString())); }
    { QSignalBlocker block(m_presets); m_presets->setItemText(0, tr("Light")); m_presets->setItemText(1, tr("Medium")); m_presets->setItemText(2, tr("Heavy")); }
    m_convert->setText(tr("Convert")); m_randomConvert->setText(tr("Random convert"));
    m_randomConvert->setToolTip(tr("Choose a new seed and convert immediately"));
    m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear")); m_copy->setText(tr("Copy output"));
    m_seedLabel->setText(tr("Seed")); m_seed->setPlaceholderText(tr("ASCII letters and digits; case-insensitive"));
    m_seed->setToolTip(tr("Up to 64 ASCII letters/digits. Numeric seeds keep their original value; letters are case-insensitive."));
    m_randomSeed->setToolTip(tr("Randomize seed (does not convert)")); m_randomSeed->setAccessibleName(tr("Randomize seed"));
    m_copySeed->setText(tr("Copy seed")); refresh();
}
void TextStylePage::changeEvent(QEvent* event) { ToolPage::changeEvent(event); if (event->type() == QEvent::LanguageChange) retranslate(); }
void TextStylePage::resizeEvent(QResizeEvent* event) {
    ToolPage::resizeEvent(event);
    const auto direction = width() >= 680 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (m_columns && m_columns->direction() != direction) m_columns->setDirection(direction);
}
