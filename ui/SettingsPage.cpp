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
SettingsPage::SettingsPage(AppSettings& settings, QWidget* parent) : QWidget(parent), m_settings(settings), m_models(new ai::AiService(this))
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
    auto* card = new QFrame(content); ui::styleCard(card);
    auto* aiLayout = new QVBoxLayout(card);
    aiLayout->setContentsMargins(20, 20, 20, 20);
    m_aiTitle = ui::label({}, 13, ui::Tone::Primary, card);
    m_aiInfo = ui::label({}, 10, ui::Tone::Secondary, card);
    aiLayout->addWidget(m_aiTitle); aiLayout->addWidget(m_aiInfo);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto field = [card, form](QLabel*& label, const char* name) {
        label = ui::label({}, 10, ui::Tone::Secondary, card);
        auto* edit = new ElaLineEdit(card); edit->setObjectName(name);
        form->addRow(label, edit); return edit;
    };
    m_apiUrl = field(m_apiUrlLabel, "aiApiUrl");
    m_apiModelLabel = ui::label({}, 10, ui::Tone::Secondary, card);
    m_apiModel = new ElaComboBox(card); m_apiModel->setObjectName("aiApiModel"); m_apiModel->setEditable(true);
    m_apiModel->setInsertPolicy(QComboBox::NoInsert);
    m_apiModel->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_apiModel->setMinimumContentsLength(16);
    auto* modelRow = new QHBoxLayout;
    m_fetchModels = new ElaPushButton(card); m_fetchModels->setObjectName("aiFetchModels");
    modelRow->addWidget(m_apiModel, 1); modelRow->addWidget(m_fetchModels);
    form->addRow(m_apiModelLabel, modelRow);
    m_apiKey = field(m_apiKeyLabel, "aiApiKey"); m_apiKey->setEchoMode(QLineEdit::Password);
    const auto config = settings.aiConfiguration();
    m_apiUrl->setText(config.baseUrl); m_apiModel->setEditText(config.model); m_apiKey->setText(config.apiKey);
    aiLayout->addLayout(form);
    m_saveAi = new ElaPushButton(card); m_saveAi->setObjectName("aiSaveSettings"); ui::primaryButton(m_saveAi);
    m_aiStatus = ui::label({}, 10, ui::Tone::Secondary, card);
    aiLayout->addWidget(m_saveAi, 0, Qt::AlignLeft); aiLayout->addWidget(m_aiStatus);
    layout->addWidget(card);
    connect(m_saveAi, &ElaPushButton::clicked, this, [this] {
        const ai::Configuration config{m_apiUrl->text(), m_apiModel->currentText(), m_apiKey->text()};
        const auto error = ai::AiService::validate(config);
        if (!error.isEmpty()) { m_aiStatus->setText(error); return; }
        if (!m_settings.setAiConfiguration(config)) m_aiStatus->setText(tr("Could not securely save the API settings."));
        else m_aiStatus->setText(tr("API settings saved. No network request was sent."));
    });
    connect(m_fetchModels, &ElaPushButton::clicked, this, &SettingsPage::fetchModels);
    connect(m_apiUrl, &QLineEdit::textChanged, this, &SettingsPage::invalidateModels);
    connect(m_apiKey, &QLineEdit::textChanged, this, &SettingsPage::invalidateModels);
    connect(m_models, &ai::AiService::modelsListed, this, [this](const QStringList& models) {
        if (!m_modelsStale) {
            const auto current = m_apiModel->currentText();
            const QSignalBlocker blocker(m_apiModel);
            m_apiModel->clear(); m_apiModel->addItems(models);
            m_apiModel->setCurrentIndex(-1); m_apiModel->setEditText(current);
            m_aiStatus->setText(tr("Found %1 models. Choose a model ID and save. The list may also include models that do not support text chat.").arg(models.size()));
        }
        updateFetchButton();
    });
    connect(m_models, &ai::AiService::failed, this, [this](const QString& error) {
        if (!m_modelsStale) m_aiStatus->setText(error);
        updateFetchButton();
    });
    connect(m_models, &ai::AiService::cancelled, this, [this] {
        if (!m_modelsStale) m_aiStatus->setText(tr("Model listing cancelled. You can enter a model ID manually."));
        updateFetchButton();
    });
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
    m_aiTitle->setText(tr("AI API (OpenAI compatible)"));
    m_aiInfo->setText(tr("Online tools send requests only when you click their AI or Generate button. The API key is encrypted for your Windows account. Voice Designer uses this address and key with its fixed MiMo model."));
    m_apiUrlLabel->setText(tr("API base URL")); m_apiModelLabel->setText(tr("Model ID")); m_apiKeyLabel->setText(tr("API key"));
    m_apiUrl->setPlaceholderText("https://api.example.com/v1"); m_apiModel->lineEdit()->setPlaceholderText(tr("Fetch models or enter the provider's model ID"));
    m_apiModel->setToolTip(tr("Use the exact model ID, not the provider's product name."));
    m_saveAi->setText(tr("Save API settings"));
    updateFetchButton();
}
void SettingsPage::updateFetchButton() {
    m_fetchModels->setText(m_models->isBusy() ? tr("Cancel") : tr("Fetch models"));
}
void SettingsPage::invalidateModels() {
    m_modelsStale = true; m_models->cancel();
    const auto current = m_apiModel->currentText();
    { const QSignalBlocker blocker(m_apiModel); m_apiModel->clear(); m_apiModel->setEditText(current); }
    m_aiStatus->clear(); updateFetchButton();
}
void SettingsPage::fetchModels() {
    if (m_models->isBusy()) { m_models->cancel(); return; }
    m_modelsStale = false;
    m_aiStatus->setText(tr("Fetching models from the entered address. Only the API key is sent; no text is uploaded."));
    m_models->listModels({m_apiUrl->text(), {}, m_apiKey->text()});
    updateFetchButton();
}
void SettingsPage::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
