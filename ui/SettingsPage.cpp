#include "SettingsPage.h"
#include "theme/Theme.h"
#include "theme/ThemeManager.h"
#include "services/LanguageManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 28, 32, 28);
    mainLayout->setSpacing(24);

    auto* titleLabel = new QLabel(tr("Settings"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::TextPrimary()));

    mainLayout->addWidget(titleLabel);

    auto* themeGroup = new QGroupBox(tr("Theme"), this);
    themeGroup->setStyleSheet(QStringLiteral(R"(
        QGroupBox {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 8px;
            margin-top: 12px;
            padding: 16px;
            font-size: 13px;
            font-weight: 600;
            color: %3;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
    )").arg(theme::Surface1(), theme::Border(), theme::TextPrimary()));

    auto* themeLayout = new QVBoxLayout(themeGroup);
    themeLayout->setSpacing(12);

    auto* themeLabel = new QLabel(tr("Color Theme"), this);
    themeLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: normal;").arg(theme::TextSecondary()));

    m_themeCombo = new QComboBox(this);
    m_themeCombo->setFixedHeight(36);
    m_themeCombo->setStyleSheet(QStringLiteral(R"(
        QComboBox {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 0 12px;
            font-size: 13px;
        }
        QComboBox:hover {
            border-color: %4;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QListView {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            outline: none;
        }
        QListView::item {
            padding: 6px 12px;
        }
        QListView::item:hover {
            background-color: %5;
        }
        QListView::item:selected {
            background-color: %4;
        }
    )").arg(theme::Surface0(), theme::TextPrimary(), theme::Border(), theme::Accent(), theme::Surface2()));

    auto presets = ThemeManager::instance().presetNames();
    for (const auto& name : presets) {
        m_themeCombo->addItem(name);
    }
    m_themeCombo->setCurrentText(ThemeManager::instance().currentPresetName());

    connect(m_themeCombo, &QComboBox::currentTextChanged, this, [](const QString& name) {
        ThemeManager::instance().applyPreset(name);
    });

    themeLayout->addWidget(themeLabel);
    themeLayout->addWidget(m_themeCombo);

    mainLayout->addWidget(themeGroup);

    auto* langGroup = new QGroupBox(tr("Language"), this);
    langGroup->setStyleSheet(QStringLiteral(R"(
        QGroupBox {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 8px;
            margin-top: 12px;
            padding: 16px;
            font-size: 13px;
            font-weight: 600;
            color: %3;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
    )").arg(theme::Surface1(), theme::Border(), theme::TextPrimary()));

    auto* langLayout = new QVBoxLayout(langGroup);
    langLayout->setSpacing(12);

    auto* langLabel = new QLabel(tr("Display Language"), this);
    langLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: normal;").arg(theme::TextSecondary()));

    m_langCombo = new QComboBox(this);
    m_langCombo->setFixedHeight(36);
    m_langCombo->setStyleSheet(QStringLiteral(R"(
        QComboBox {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 0 12px;
            font-size: 13px;
        }
        QComboBox:hover {
            border-color: %4;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QListView {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            outline: none;
        }
        QListView::item {
            padding: 6px 12px;
        }
        QListView::item:hover {
            background-color: %5;
        }
        QListView::item:selected {
            background-color: %4;
        }
    )").arg(theme::Surface0(), theme::TextPrimary(), theme::Border(), theme::Accent(), theme::Surface2()));

    auto& langMgr = LanguageManager::instance();
    auto locales = langMgr.availableLocales();
    for (const auto& locale : locales) {
        m_langCombo->addItem(langMgr.localeName(locale), locale);
    }
    m_langCombo->setCurrentText(langMgr.localeName(langMgr.currentLocale()));

    connect(m_langCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        QString locale = m_langCombo->itemData(index).toString();
        emit languageChanged(locale);
    });

    langLayout->addWidget(langLabel);
    langLayout->addWidget(m_langCombo);

    mainLayout->addWidget(langGroup);
    mainLayout->addStretch();
}
