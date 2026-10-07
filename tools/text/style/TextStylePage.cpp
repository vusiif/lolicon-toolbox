#include "TextStylePage.h"
#include "services/AppSettings.h"
#include "ai/AiService.h"
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
#include <QCheckBox>
#include <QJsonArray>
#include <QGridLayout>
#include <QtConcurrentRun>
using namespace textstyle;
namespace {
void selection(ElaPushButton* button, bool active) {
    button->setChecked(active);
    button->setLightDefaultColor(eTheme->getThemeColor(ElaThemeType::Light,active ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase));
    button->setLightHoverColor(eTheme->getThemeColor(ElaThemeType::Light,active ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover));
    button->setLightPressColor(eTheme->getThemeColor(ElaThemeType::Light,active ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress));
    button->setLightTextColor(eTheme->getThemeColor(ElaThemeType::Light,active ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText));
    button->setDarkDefaultColor(eTheme->getThemeColor(ElaThemeType::Dark,active ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBase));
    button->setDarkHoverColor(eTheme->getThemeColor(ElaThemeType::Dark,active ? ElaThemeType::PrimaryHover : ElaThemeType::BasicHover));
    button->setDarkPressColor(eTheme->getThemeColor(ElaThemeType::Dark,active ? ElaThemeType::PrimaryPress : ElaThemeType::BasicPress));
    button->setDarkTextColor(eTheme->getThemeColor(ElaThemeType::Dark,active ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText));
    button->update();
}
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
TextStylePage::TextStylePage(QWidget* parent, AppSettings* settings) : ToolPage(parent), m_settings(settings), m_loaded(Loader::builtin()), m_aiService(new ai::AiService(this)), m_runner(Engine::transform) {
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
    m_encodeMode=button("textStyleEncodeMode"); m_restoreMode=button("textStyleRestoreMode");
    m_encodeMode->setCheckable(true); m_restoreMode->setCheckable(true); m_encodeMode->setChecked(true);
    auto* modes=new QHBoxLayout; modes->addWidget(m_encodeMode); modes->addWidget(m_restoreMode); modes->addStretch();
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
    m_seedArea=new QWidget(content); auto* seedRow = new QHBoxLayout(m_seedArea); seedRow->setContentsMargins(0,0,0,0); seedRow->addWidget(m_seed, 1); seedRow->addWidget(m_randomSeed); seedRow->addWidget(m_copySeed);
    form->addRow(m_seedLabel, m_seedArea);
    m_restoreArea=new QWidget(content); auto* restoration=new QVBoxLayout(m_restoreArea); restoration->setContentsMargins(0,0,0,0);
    m_removeWrappers=new QCheckBox(m_restoreArea); m_removeWrappers->setObjectName("textStyleRemoveWrappers"); m_removeWrappers->setChecked(true); restoration->addWidget(m_removeWrappers);
    auto* choices=new QGridLayout; m_restoreChoiceLayout=choices;
    for(int i=0;i<5;++i) { auto* b=button(qPrintable(QString("textStyleRestoreCandidate%1").arg(i))); b->setCheckable(true); m_restoreChoices[i]=b; choices->addWidget(b,0,i);
        connect(b,&ElaPushButton::clicked,this,[this,i] { if(i<m_restored.candidates.size()) { m_selectedRestore=i; m_showAi=false; m_copied=false; showOutput(); refresh(); } }); }
    restoration->addLayout(choices);
    auto* views=new QHBoxLayout; m_localView=button("textStyleLocalView"); m_aiView=button("textStyleAiView");
    views->addWidget(m_localView); views->addWidget(m_aiView); views->addStretch(); right->insertLayout(1,views);
    m_localView->setCheckable(true); m_aiView->setCheckable(true);
    // Keep the editor headers equal in height so both action rows stay aligned.
    auto* leftHeader=new QWidget(content); leftHeader->setFixedHeight(m_localView->height()); left->insertWidget(1,leftHeader);
    m_aiRun=button("textStyleAiRun"); m_aiCancel=button("textStyleAiCancel");
    auto* aiActions=new QHBoxLayout; aiActions->addWidget(m_aiRun); aiActions->addWidget(m_aiCancel); aiActions->addStretch();
    m_aiHint=ui::label({},9,ui::Tone::Secondary,content); m_aiStatus=ui::label({},9,ui::Tone::Secondary,content);
    m_aiHint->setTextFormat(Qt::PlainText); m_aiStatus->setTextFormat(Qt::PlainText); m_aiStatus->setObjectName("textStyleAiStatus");
    layout->addWidget(m_title); layout->addWidget(m_description); layout->addLayout(modes); layout->addLayout(options); layout->addLayout(form); layout->addWidget(m_restoreArea); layout->addLayout(columns); layout->addWidget(m_status);
    layout->addLayout(aiActions); layout->addWidget(m_aiHint); layout->addWidget(m_aiStatus);
    layout->addStretch();
    connect(m_input, &ElaPlainTextEdit::textChanged, this, &TextStylePage::invalidate);
    connect(m_seed, &QLineEdit::textChanged, this, &TextStylePage::invalidate);
    connect(m_styles, &QComboBox::currentIndexChanged, this, &TextStylePage::invalidate);
    connect(m_presets, &QComboBox::currentIndexChanged, this, [this] { invalidate(); savePreferences(); });
    auto selectMode=[this](bool reverse) { m_encodeMode->setChecked(!reverse); m_restoreMode->setChecked(reverse); invalidate(); retranslate(); };
    connect(m_encodeMode,&ElaPushButton::clicked,this,[selectMode] { selectMode(false); });
    connect(m_restoreMode,&ElaPushButton::clicked,this,[selectMode] { selectMode(true); });
    connect(m_removeWrappers,&QCheckBox::toggled,this,&TextStylePage::invalidate);
    connect(m_convert, &ElaPushButton::clicked, this, &TextStylePage::run);
    connect(m_randomConvert, &ElaPushButton::clicked, this, [this] {
        if (busy() || inputText().isEmpty() || !m_loaded.style || restoring()) return;
        randomizeSeed(); run();
    });
    connect(m_randomSeed, &ElaIconButton::clicked, this, &TextStylePage::randomizeSeed);
    connect(m_cancel, &ElaPushButton::clicked, this, [this] { invalidate(); m_error = Error::Cancelled; refresh(); });
    connect(m_clear, &ElaPushButton::clicked, this, [this] { m_input->clear(); invalidate(); });
    connect(m_copy, &ElaPushButton::clicked, this, [this] { if (m_copy->isEnabled()) { m_copied = m_clipboardWriter(Engine::normalize(m_output->document()->toRawText())); m_clipboardFailed = !m_copied; refresh(); } });
    connect(m_copySeed, &ElaPushButton::clicked, this, [this] { UserSeed seed; if (Engine::userSeed(m_seed->text(), seed)) { m_copied = m_clipboardWriter(seed.text); m_clipboardFailed = !m_copied; refresh(); } });
    connect(&m_worker, &QFutureWatcher<Result>::finished, this, [this] {
        if (m_submittedRevision != m_revision) { refresh(); return; }
        auto result = m_worker.result(); m_error = result.error; m_success = result.success();
        if (m_success) { m_result = std::move(result); const QSignalBlocker block(m_seed); m_seed->setText(m_result.metadata["seed_text"].toString()); showOutput(); }
        refresh();
    });
    connect(&m_restoreWorker,&QFutureWatcher<Restoration>::finished,this,[this] {
        if(m_submittedRevision!=m_revision) { refresh(); return; }
        m_restored=m_restoreWorker.result(); m_error=m_restored.error; m_success=m_restored.success(); m_selectedRestore=0; showOutput(); refresh();
    });
    connect(m_localView,&ElaPushButton::clicked,this,[this] { m_showAi=false; showOutput(); refresh(); });
    connect(m_aiView,&ElaPushButton::clicked,this,[this] { m_showAi=true; showOutput(); refresh(); });
    connect(m_aiRun,&ElaPushButton::clicked,this,&TextStylePage::runAi);
    connect(m_aiCancel,&ElaPushButton::clicked,m_aiService,&ai::AiService::cancel);
    connect(m_aiService,&ai::AiService::completed,this,[this](const ai::Reply& reply) {
        m_aiTimer.stop(); if(m_aiRevision!=m_revision) { refresh(); return; }
        m_aiReply=reply; m_showAi=!reply.text.isEmpty(); m_aiMessage=reply.reason.left(1600);
        if(!reply.guesses.isEmpty()) m_aiMessage += "\n"+reply.guesses.mid(0,8).join('\n').left(1600);
        showOutput(); refresh();
    });
    connect(m_aiService,&ai::AiService::failed,this,[this](const QString& error) { m_aiTimer.stop(); if(m_aiRevision==m_revision) m_aiMessage=error; refresh(); });
    connect(m_aiService,&ai::AiService::cancelled,this,[this] { m_aiTimer.stop(); if(m_aiRevision==m_revision) m_aiMessage=tr("AI request cancelled."); refresh(); });
    connect(m_aiService,&ai::AiService::progress,this,[this](bool thinking,int characters) { m_aiThinking=thinking; m_aiCharacters=characters; refreshAi(); });
    m_aiTimer.setInterval(500); connect(&m_aiTimer,&QTimer::timeout,this,&TextStylePage::refreshAi);
    if(m_settings) connect(m_settings,&AppSettings::aiConfigurationChanged,this,[this] { invalidate(); });
    retranslate();
}
TextStylePage::~TextStylePage() { if (m_cancelToken) m_cancelToken->store(true); m_aiService->disconnect(this); m_aiService->cancel(); }
bool TextStylePage::restoring() const { return m_restoreMode->isChecked(); }
bool TextStylePage::busy() const { return m_worker.isRunning() || m_restoreWorker.isRunning() || m_aiService->isBusy(); }
QString TextStylePage::inputText() const { return m_input->document()->toRawText(); }
void TextStylePage::savePreferences() { if (m_settings) m_settings->setTextStylePreset(m_presets->currentData().toString()); }
void TextStylePage::invalidate() {
    ++m_revision; if (m_cancelToken) m_cancelToken->store(true); m_success = false; m_result = {}; m_error = Error::None; m_copied = false; m_clipboardFailed = false; m_output->clear(); refresh();
    m_aiRevision=UINT64_MAX; m_aiTimer.stop(); m_aiService->cancel(); m_aiReply={}; m_showAi=false; m_aiMessage.clear(); m_restored={}; m_selectedRestore=0; refresh();
}
void TextStylePage::randomizeSeed() {
    UserSeed current; const bool valid = Engine::userSeed(m_seed->text(), current);
    auto candidate = QRandomGenerator::system()->generate64();
    if (valid && candidate == current.value) ++candidate;
    m_seed->setText(QString::number(candidate));
}
void TextStylePage::run() {
    if (busy() || inputText().isEmpty()) return;
    if(restoring()) {
        invalidate(); m_submittedRevision=m_revision; m_cancelToken=std::make_shared<std::atomic_bool>(false);
        const auto input=inputText(); const auto style=m_loaded.style; const auto token=m_cancelToken; const bool wrappers=m_removeWrappers->isChecked();
        m_restoreWorker.setFuture(QtConcurrent::run([input,style,token,wrappers] { return Restore::run(input,style,wrappers,Control{token}); })); refresh(); return;
    }
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
    const bool running = m_worker.isRunning() || m_restoreWorker.isRunning(); const auto input = inputText(); UserSeed seed;
    const bool validSeed = Engine::userSeed(m_seed->text(), seed);
    m_convert->setEnabled(!busy() && !input.isEmpty() && (restoring() || validSeed) && bool(m_loaded.style)); m_cancel->setVisible(running);
    m_randomConvert->setEnabled(!busy() && !input.isEmpty() && bool(m_loaded.style)); m_randomConvert->setVisible(!restoring());
    m_copy->setEnabled((m_showAi ? !m_aiReply.text.isEmpty() : m_success) && !busy()); m_copySeed->setEnabled(validSeed);
    m_seedArea->setVisible(!restoring()); m_seedLabel->setVisible(!restoring()); m_presets->setVisible(!restoring()); m_presetLabel->setVisible(!restoring()); m_restoreArea->setVisible(restoring());
    selection(m_encodeMode,!restoring()); selection(m_restoreMode,restoring());
    for(int i=0;i<5;++i) { auto* b=m_restoreChoices[i]; b->setEnabled(m_success && i<m_restored.candidates.size()); selection(b,!m_showAi && i==m_selectedRestore && m_success);
        b->setVisible(i==0 || (m_success && i<m_restored.candidates.size()));
        b->setText(i==0 ? tr("Keep uncertain") : i<m_restored.candidates.size() && m_restored.candidates[i]==Engine::normalize(input) ? tr("Original") : tr("Candidate %1").arg(i+1)); }
    QStringList ambiguityHints;
    for(const auto& entry : m_restored.ambiguities) { const auto a=entry.toObject(); QStringList choices; for(const auto& c:a["sources"].toArray()) choices.append(c.toString()); ambiguityHints.append(a["text"].toString()+" → "+choices.join(" / ")); }
    m_status->setToolTip(ambiguityHints.join('\n'));
    m_localView->setEnabled(m_success); m_aiView->setEnabled(!m_aiReply.text.isEmpty());
    selection(m_localView,m_success && !m_showAi); selection(m_aiView,m_showAi && !m_aiReply.text.isEmpty());
    m_outputLabel->setText(m_showAi ? tr("AI suggestion") : restoring() ? tr("Restoration candidate") : tr("Styled text"));
    if (!m_loaded.style) m_status->setText(tr("Style unavailable: %1").arg(m_loaded.detail));
    else if (running) m_status->setText(tr("Converting locally… Editing input or options cancels this snapshot."));
    else if ((!restoring() && !validSeed) || m_error == Error::InvalidSeed) m_status->setText(tr("Enter a seed or use the dice button."));
    else if (m_error != Error::None) {
        const auto name = errorName(m_error);
        if (m_error == Error::Cancelled) m_status->setText(tr("Conversion cancelled."));
        else if (m_error == Error::InputTooLarge) m_status->setText(tr("Input exceeds 256 KiB after newline normalization or the UTF-16 limit."));
        else if (m_error == Error::OutputTooLarge) m_status->setText(tr("Output exceeds 2 MiB. Use a shorter input or a lighter preset."));
        else if (m_error == Error::InvalidUnicodeInput) m_status->setText(tr("Input contains invalid Unicode surrogate sequences."));
        else if (m_error == Error::Timeout) m_status->setText(tr("Conversion exceeded the 5-second budget. Use a smaller input."));
        else m_status->setText(tr("Conversion failed (%1). Check the style and options.").arg(name));
    } else if (m_success && restoring()) {
        m_status->setText(tr("Local restoration: %1 matches, %2 ambiguous spans, %3 wrapper pairs. Candidates are possibilities, not verified originals.").arg(m_restored.matched).arg(m_restored.ambiguous).arg(m_restored.wrappers));
    } else if (m_success) {
        const auto& s = m_result.stats;
        m_status->setText(tr("Matched %1 · replaced %2 · decorated lines %3 · output %4 bytes.").arg(s.matched).arg(s.replaced).arg(s.decorated).arg(s.outputBytes) +
            (s.matched == 0 && s.decorated == 0 ? " " + tr("No rules matched; only newline normalization was applied.") : QString()) + (m_copied ? " " + tr("Copied.") : QString()));
    } else m_status->setText(input.isEmpty() ? tr("Enter text to convert. Text and seeds are not saved between sessions.") : tr("Ready. Convert uses the displayed seed. Use the dice button to change it."));
    if (m_clipboardFailed) m_status->setText(m_status->text() + " " + tr("Clipboard ownership was not obtained. Another application may be using it; retry copying."));
    refreshAi();
}
void TextStylePage::retranslate() {
    m_title->setText(tr("Text Style Converter")); m_description->setText(tr("Local styling and restoration candidates. AI assistance sends text only when you click its button."));
    m_encodeMode->setText(tr("Style text")); m_restoreMode->setText(tr("Restore text"));
    m_removeWrappers->setText(tr("Remove recognized paired line decorations"));
    m_styleLabel->setText(tr("Style")); m_presetLabel->setText(tr("Intensity")); m_inputLabel->setText(tr("Input text")); m_outputLabel->setText(tr("Result"));
    const auto locale = m_settings ? m_settings->locale() : "en";
    if (m_loaded.style) { QSignalBlocker block(m_styles); m_styles->setItemText(0, m_loaded.style->names.value(locale).toString(m_loaded.style->names["en"].toString())); }
    { QSignalBlocker block(m_presets); m_presets->setItemText(0, tr("Light")); m_presets->setItemText(1, tr("Medium")); m_presets->setItemText(2, tr("Heavy")); }
    m_convert->setText(restoring() ? tr("Restore locally") : tr("Convert")); m_randomConvert->setText(tr("Random convert"));
    m_randomConvert->setToolTip(tr("Choose a new seed and convert immediately"));
    m_cancel->setText(tr("Cancel")); m_clear->setText(tr("Clear")); m_copy->setText(tr("Copy output"));
    m_seedLabel->setText(tr("Seed")); m_seed->setPlaceholderText(tr("ASCII letters and digits; case-insensitive"));
    m_seed->setToolTip(tr("Up to 64 ASCII letters/digits. Numeric seeds keep their original value; letters are case-insensitive."));
    m_randomSeed->setToolTip(tr("Randomize seed (does not convert)")); m_randomSeed->setAccessibleName(tr("Randomize seed"));
    m_copySeed->setText(tr("Copy seed")); refresh();
}
void TextStylePage::showOutput() {
    const auto text=m_showAi ? m_aiReply.text : restoring() ? m_restored.candidates.value(m_selectedRestore) : m_result.output;
    m_output->setPlainText(text);
}
void TextStylePage::runAi() {
    if(busy() || !m_settings || inputText().isEmpty()) return;
    m_aiReply={}; m_showAi=false; m_aiMessage.clear(); showOutput(); m_aiRevision=m_revision;
    QJsonArray candidates;
    if(m_success && restoring()) for(const auto& text:m_restored.candidates) candidates.append(QJsonObject{{"text",text}});
    else if(m_success) candidates.append(QJsonObject{{"text",m_result.output}});
    if(candidates.isEmpty()) candidates.append(QJsonObject{{"text",Engine::normalize(inputText())},{"is_original",true}});
    UserSeed seed; Engine::userSeed(m_seed->text(),seed);
    const auto original=Engine::normalize(inputText());
    QJsonArray hints;
    if(!restoring() && m_loaded.style) for(const auto& rule:m_loaded.style->rules) {
        if(!original.contains(rule.source)) continue;
        QJsonArray alternatives; for(const auto& choice:rule.choices) alternatives.append(choice.text);
        hints.append(QJsonObject{{"source",rule.source},{"alternatives",alternatives}}); if(hints.size()==32) break;
    }
    QJsonObject context{{"task",restoring() ? "text_style_restore" : "text_style_encode"},{"original_text",original},
        {"candidates",candidates},{"ambiguities",m_restored.ambiguities},{"style_id","mars_classic"},
        {"intensity",m_presets->currentData().toString()},{"seed",seed.text},{"rule_hints",hints},{"remove_line_decorations",m_removeWrappers->isChecked()}};
    m_aiThinking=false; m_aiCharacters=0; m_aiElapsed.start();
    m_aiService->analyze(m_settings->aiConfiguration(),context,m_settings->locale());
    if(m_aiService->isBusy()) m_aiTimer.start(); refresh();
}
void TextStylePage::refreshAi() {
    const auto config=m_settings ? m_settings->aiConfiguration() : ai::Configuration{};
    const bool configured=m_settings && ai::AiService::validate(config).isEmpty();
    m_aiRun->setText(restoring() ? tr("AI restoration") : tr("AI styling")); m_aiCancel->setText(tr("Cancel AI"));
    m_localView->setText(tr("Local result")); m_aiView->setText(tr("AI suggestion"));
    m_aiRun->setEnabled(configured && !busy() && !inputText().isEmpty() && Engine::normalize(inputText()).toUtf8().size()<=16*1024);
    m_aiCancel->setVisible(m_aiService->isBusy());
    m_aiHint->setText(configured ? tr("AI sends the input and available local candidates to %1 (model %2). Maximum input: 16 KiB. AI output is an inference and may vary.").arg(config.baseUrl,config.model) : tr("Configure the AI address and model in Settings. Local operations stay offline."));
    if(m_aiService->isBusy() && m_aiRevision==m_revision)
        m_aiStatus->setText(tr("AI running: %1 s / 90 s, %2 characters received (%3).").arg(m_aiElapsed.elapsed()/1000).arg(m_aiCharacters).arg(m_aiThinking ? tr("thinking") : tr("reply")));
    else m_aiStatus->setText(m_aiMessage);
}
void TextStylePage::changeEvent(QEvent* event) { ToolPage::changeEvent(event); if (event->type() == QEvent::LanguageChange) retranslate(); }
void TextStylePage::resizeEvent(QResizeEvent* event) {
    ToolPage::resizeEvent(event);
    const auto direction = width() >= 680 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (m_columns && m_columns->direction() != direction) m_columns->setDirection(direction);
    if(m_restoreChoiceLayout) { const int columns=width()>=680 ? 5 : 2; for(int i=0;i<5;++i) { m_restoreChoiceLayout->removeWidget(m_restoreChoices[i]); m_restoreChoiceLayout->addWidget(m_restoreChoices[i],i/columns,i%columns); } }
}
