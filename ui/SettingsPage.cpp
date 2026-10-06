#include "SettingsPage.h"
#include "UiTheme.h"
#include "services/AppSettings.h"
#include <ElaComboBox.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QEvent>
#include <QSignalBlocker>
SettingsPage::SettingsPage(AppSettings& settings, QWidget* parent) : QWidget(parent), m_settings(settings)
{
    setObjectName("settingsPage");
    auto* layout = new QVBoxLayout(this);
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
}
void SettingsPage::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
