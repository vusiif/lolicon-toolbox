#include "MainWindow.h"
#include "Sidebar.h"
#include "SettingsPage.h"
#include "theme/Theme.h"
#include "theme/ThemeManager.h"
#include "services/LanguageManager.h"
#include "navigation/NavigationManager.h"
#include "tool/ToolRegistry.h"
#include "tool/ToolPageManager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

#include <windows.h>
#include <dwmapi.h>

MainWindow::MainWindow(NavigationManager* navManager,
                       ToolRegistry* registry,
                       ToolPageManager* pageManager,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_navManager(navManager)
    , m_registry(registry)
    , m_pageManager(pageManager)
{
    setupUi();
    setWindowTitle("Toolbox");
    resize(960, 640);
}

void MainWindow::setupUi()
{
    setStyleSheet(theme::globalStyleSheet());

    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    auto* layout = new QHBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_sidebar = new Sidebar(this);
    m_sidebar->build(m_navManager, m_registry);
    m_sidebar->setFixedWidth(220);

    auto* placeholder = new QLabel(tr("Select a tool from the sidebar"), this);
    placeholder->setObjectName("placeholderLabel");
    placeholder->setAlignment(Qt::AlignCenter);
    QFont placeholderFont = placeholder->font();
    placeholderFont.setPointSize(13);
    placeholder->setFont(placeholderFont);
    placeholder->setStyleSheet(QStringLiteral("color: %1;").arg(theme::TextDisabled()));

    m_pageStack = m_pageManager->stackWidget();
    m_pageStack->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::WindowBg()));
    m_pageStack->addWidget(placeholder);

    m_settingsPage = new SettingsPage(this);
    m_pageStack->addWidget(m_settingsPage);

    layout->addWidget(m_sidebar);
    layout->addWidget(m_pageStack, 1);

    setCentralWidget(centralWidget);

    connect(m_sidebar, &Sidebar::toolSelected, this, [this](const QString& toolId) {
        if (m_pageManager) {
            m_pageManager->openTool(toolId);

            auto* newPage = m_pageStack->currentWidget();
            if (newPage) {
                auto* effect = new QGraphicsOpacityEffect(newPage);
                newPage->setGraphicsEffect(effect);

                auto* fadeAnim = new QPropertyAnimation(effect, "opacity");
                fadeAnim->setDuration(200);
                fadeAnim->setStartValue(0.0);
                fadeAnim->setEndValue(1.0);
                fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
                fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
            }
        }
    });

    connect(m_sidebar, &Sidebar::settingsClicked, this, [this]() {
        m_pageStack->setCurrentWidget(m_settingsPage);

        auto* effect = new QGraphicsOpacityEffect(m_settingsPage);
        m_settingsPage->setGraphicsEffect(effect);

        auto* fadeAnim = new QPropertyAnimation(effect, "opacity");
        fadeAnim->setDuration(200);
        fadeAnim->setStartValue(0.0);
        fadeAnim->setEndValue(1.0);
        fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
        fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
    });

    connect(m_settingsPage, &SettingsPage::languageApplyRequested, this, [this](const QString& locale) {
        LanguageManager::instance().setLanguage(locale);
    });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        setStyleSheet(theme::globalStyleSheet());
        m_pageStack->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::WindowBg()));
    });

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        // Retranslate UI
        auto* placeholder = m_pageStack->findChild<QLabel*>("placeholderLabel");
        if (placeholder) {
            placeholder->setText(tr("Select a tool from the sidebar"));
        }
    });
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
}
