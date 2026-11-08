#include "SettingsPage.h"
#include "UiTheme.h"
#include "services/AppSettings.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <QFormLayout>
#include "ai/AiService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QEvent>
#include <QSignalBlocker>
#include <algorithm>
namespace {
// Provider model IDs vary ("mimo-v2.5-tts-voicedesign", "...-voice-design"); match the concept, not one ID.
bool looksLikeVoiceDesign(const QString& id) {
    auto folded = id.toLower(); folded.remove('-'); folded.remove('_');
    return folded.contains("voicedesign");
}
}
ai::Configuration SettingsPage::ApiForm::input() const { return {url->text(), model->currentText(), key->text()}; }
SettingsPage::SettingsPage(AppSettings& settings, QWidget* parent) : QWidget(parent), m_settings(settings)
{
    setObjectName("settingsPage");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    content->setAutoFillBackground(false); scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);
    m_title = ui::label({}, 18, ui::Tone::Primary, this);
    layout->addWidget(m_title);
    auto row = [this, layout](QLabel*& label, ElaComboBox*& combo, const char* name) {
        auto* card = new QFrame(this);
        ui::styleCard(card);
        auto* box = new QHBoxLayout(card);
        box->setContentsMargins(20, 20, 20, 20);
        label = ui::label({}, 11, ui::Tone::Primary, card);
        combo = new ElaComboBox(card);
        combo->setObjectName(name);
        combo->setMinimumWidth(180);
        box->addWidget(label, 1);
        box->addWidget(combo);
        layout->addWidget(card);
    };
    row(m_languageLabel, m_language, "languageCombo");
    m_language->addItem(QString(), QStringLiteral("system"));
    m_language->addItem("English", "en");
    m_language->addItem(QString::fromUtf8("简体中文"), "zh_CN");
    m_language->addItem(QString::fromUtf8("繁體中文"), "zh_TW");
    m_error = ui::label({}, 10, ui::Tone::Danger, this);
    layout->addWidget(m_error);
    for (const auto api : {Api::Text, Api::Voice}) {
        layout->addWidget(buildApiForm(form(api), api, api == Api::Text ? "ai" : "voice",
            api == Api::Text ? settings.aiConfiguration() : settings.voiceConfiguration()));
    }
    layout->addStretch();
    retranslate();
    synchronize();
    connect(m_language, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_settings.setLocale(m_language->currentData().toString())) {
            m_error->setText(tr("Could not load translation."));
            synchronize();
        } else m_error->clear();
    });
    connect(&settings, &AppSettings::localeChanged, this, &SettingsPage::synchronize);
    // While the voice API is unsaved it mirrors the text API. Follow that in the voice
    // fields unless the user has already started editing them.
    m_voiceMirror = settings.voiceConfiguration();
    connect(&settings, &AppSettings::aiConfigurationChanged, this, [this] {
        if (m_settings.hasVoiceConfiguration()) return;
        if (m_voice.url->text() != m_voiceMirror.baseUrl || m_voice.key->text() != m_voiceMirror.apiKey) return;
        m_voiceMirror = m_settings.voiceConfiguration();
        m_voice.url->setText(m_voiceMirror.baseUrl); m_voice.key->setText(m_voiceMirror.apiKey);
    });
}
QWidget* SettingsPage::buildApiForm(ApiForm& apiForm, Api api, const char* prefix, const ai::Configuration& config)
{
    const auto name = [prefix](const char* suffix) { return QString::fromLatin1(prefix) + QString::fromLatin1(suffix); };
    auto* card = new QFrame(this); ui::styleCard(card);
    auto* box = new QVBoxLayout(card);
    box->setContentsMargins(20, 20, 20, 20);
    apiForm.title = ui::label({}, 13, ui::Tone::Primary, card);
    apiForm.info = ui::label({}, 10, ui::Tone::Secondary, card); apiForm.info->setWordWrap(true);
    box->addWidget(apiForm.title); box->addWidget(apiForm.info);
    auto* layout = new QFormLayout;
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto field = [card, layout](QLabel*& label, const QString& objectName) {
        label = ui::label({}, 10, ui::Tone::Secondary, card);
        auto* edit = new ElaLineEdit(card); edit->setObjectName(objectName);
        layout->addRow(label, edit); return edit;
    };
    apiForm.url = field(apiForm.urlLabel, name("ApiUrl"));
    apiForm.modelLabel = ui::label({}, 10, ui::Tone::Secondary, card);
    apiForm.model = new ElaComboBox(card); apiForm.model->setObjectName(name("ApiModel")); apiForm.model->setEditable(true);
    apiForm.model->setInsertPolicy(QComboBox::NoInsert);
    apiForm.model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    apiForm.model->setMinimumContentsLength(16);
    auto* modelRow = new QHBoxLayout;
    apiForm.fetch = new ElaPushButton(card); apiForm.fetch->setObjectName(name("FetchModels"));
    modelRow->addWidget(apiForm.model, 1); modelRow->addWidget(apiForm.fetch);
    layout->addRow(apiForm.modelLabel, modelRow);
    apiForm.key = field(apiForm.keyLabel, name("ApiKey")); apiForm.key->setEchoMode(QLineEdit::Password);
    apiForm.url->setText(config.baseUrl); apiForm.model->setEditText(config.model); apiForm.key->setText(config.apiKey);
    box->addLayout(layout);
    apiForm.save = new ElaPushButton(card); apiForm.save->setObjectName(name("SaveSettings")); ui::primaryButton(apiForm.save);
    apiForm.status = ui::label({}, 10, ui::Tone::Secondary, card); apiForm.status->setWordWrap(true);
    box->addWidget(apiForm.save, 0, Qt::AlignLeft); box->addWidget(apiForm.status);
    apiForm.models = new ai::AiService(this);
    connect(apiForm.save, &ElaPushButton::clicked, this, [this, api] { saveApi(api); });
    connect(apiForm.fetch, &ElaPushButton::clicked, this, [this, api] { fetchModels(api); });
    connect(apiForm.url, &QLineEdit::textChanged, this, [this, api] { invalidateModels(api); });
    connect(apiForm.key, &QLineEdit::textChanged, this, [this, api] { invalidateModels(api); });
    connect(apiForm.models, &ai::AiService::modelsListed, this, [this, api](const QStringList& models) { modelsListed(api, models); });
    connect(apiForm.models, &ai::AiService::failed, this, [this, api](const QString& error) {
        if (!form(api).stale) form(api).status->setText(error);
        updateFetchButton(api);
    });
    connect(apiForm.models, &ai::AiService::cancelled, this, [this, api] {
        if (!form(api).stale) form(api).status->setText(tr("Model listing cancelled. You can enter a model ID manually."));
        updateFetchButton(api);
    });
    return card;
}
void SettingsPage::saveApi(Api api)
{
    auto& apiForm = form(api);
    const auto config = apiForm.input();
    const auto error = ai::AiService::validate(config);
    if (!error.isEmpty()) { apiForm.status->setText(error); return; }
    const bool saved = api == Api::Text ? m_settings.setAiConfiguration(config) : m_settings.setVoiceConfiguration(config);
    apiForm.status->setText(saved ? tr("API settings saved. No network request was sent.") : tr("Could not securely save the API settings."));
    if (saved && api == Api::Voice) retranslate(); // The "not saved yet" note no longer applies.
}
void SettingsPage::modelsListed(Api api, const QStringList& listed)
{
    auto& apiForm = form(api);
    if (apiForm.stale) { updateFetchButton(api); return; }
    auto current = apiForm.model->currentText();
    auto models = listed;
    QString picked;
    if (api == Api::Voice) {
        // Voice design models first; pick one unless the entered model is offered by this endpoint.
        std::stable_partition(models.begin(), models.end(), looksLikeVoiceDesign);
        if (!models.isEmpty() && looksLikeVoiceDesign(models.first()) && !models.contains(current)) picked = current = models.first();
    }
    {
        const QSignalBlocker blocker(apiForm.model);
        apiForm.model->clear(); apiForm.model->addItems(models);
        apiForm.model->setCurrentIndex(-1); apiForm.model->setEditText(current);
    }
    if (api == Api::Text)
        apiForm.status->setText(tr("Found %1 models. Choose a model ID and save. The list may also include models that do not support text chat.").arg(models.size()));
    else if (!picked.isEmpty())
        apiForm.status->setText(tr("Found %1 models and selected the voice design model %2. Save to use it.").arg(models.size()).arg(picked));
    else if (!models.isEmpty() && looksLikeVoiceDesign(models.first()))
        apiForm.status->setText(tr("Found %1 models. The entered model is offered by this API; save to use it.").arg(models.size()));
    else
        apiForm.status->setText(tr("Found %1 models, but none looks like a voice design model. Check the address or enter the model ID manually.").arg(models.size()));
    updateFetchButton(api);
}
void SettingsPage::synchronize()
{
    const QSignalBlocker languageBlock(m_language);
    m_language->setCurrentIndex(m_language->findData(m_settings.localePreference()));
}
void SettingsPage::retranslate()
{
    const QSignalBlocker blocker(m_language);
    m_title->setText(tr("Settings"));
    m_languageLabel->setText(tr("Display Language"));
    m_language->setItemText(0, tr("Follow system"));
    m_text.title->setText(tr("AI API (OpenAI compatible)"));
    m_text.info->setText(tr("Used by the text AI tools. Requests are sent only when you click an AI button. The API key is encrypted for your Windows account."));
    m_voice.title->setText(tr("Voice API (MiMo VoiceDesign)"));
    m_voice.info->setText(m_settings.hasVoiceConfiguration()
        ? tr("Used by Voice Designer. Requests are sent only when you click Generate preview. The API key is encrypted for your Windows account.")
        : tr("Used by Voice Designer. Not saved yet: it currently uses the text API address and key. Fetch models to find the voice design model, then save."));
    for (const auto api : {Api::Text, Api::Voice}) {
        auto& apiForm = form(api);
        apiForm.urlLabel->setText(tr("API base URL")); apiForm.modelLabel->setText(tr("Model ID")); apiForm.keyLabel->setText(tr("API key"));
        apiForm.model->setToolTip(tr("Use the exact model ID, not the provider's product name."));
        apiForm.save->setText(tr("Save API settings"));
        updateFetchButton(api);
    }
    m_text.url->setPlaceholderText("https://api.example.com/v1");
    m_text.model->lineEdit()->setPlaceholderText(tr("Fetch models or enter the provider's model ID"));
    m_voice.url->setPlaceholderText(AppSettings::defaultVoiceBaseUrl());
    m_voice.model->lineEdit()->setPlaceholderText(AppSettings::defaultVoiceModel());
}
void SettingsPage::updateFetchButton(Api api) {
    auto& apiForm = form(api);
    apiForm.fetch->setText(apiForm.models->isBusy() ? tr("Cancel") : tr("Fetch models"));
}
void SettingsPage::invalidateModels(Api api) {
    auto& apiForm = form(api);
    apiForm.stale = true; apiForm.models->cancel();
    const auto current = apiForm.model->currentText();
    { const QSignalBlocker blocker(apiForm.model); apiForm.model->clear(); apiForm.model->setEditText(current); }
    apiForm.status->clear(); updateFetchButton(api);
}
void SettingsPage::fetchModels(Api api) {
    auto& apiForm = form(api);
    if (apiForm.models->isBusy()) { apiForm.models->cancel(); return; }
    apiForm.stale = false;
    apiForm.status->setText(tr("Fetching models from the entered address. Only the API key is sent; no text is uploaded."));
    apiForm.models->listModels({apiForm.url->text(), {}, apiForm.key->text()});
    updateFetchButton(api);
}
void SettingsPage::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
