#include "MainWindow.h"
#include "Sidebar.h"
#include "Theme.h"
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

    auto* versionLabel = new QLabel("v0.1.0", m_sidebar->bottomArea());
    versionLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(theme::TextDisabled));
    auto* bottomLayout = new QHBoxLayout(m_sidebar->bottomArea());
    bottomLayout->setContentsMargins(16, 0, 16, 0);
    bottomLayout->addWidget(versionLabel);

    auto* placeholder = new QLabel("Select a tool from the sidebar", this);
    placeholder->setAlignment(Qt::AlignCenter);
    QFont placeholderFont = placeholder->font();
    placeholderFont.setPointSize(13);
    placeholder->setFont(placeholderFont);
    placeholder->setStyleSheet(QStringLiteral("color: %1;").arg(theme::TextDisabled));

    m_pageStack = new QStackedWidget(this);
    m_pageStack->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::WindowBg));
    m_pageStack->addWidget(placeholder);

    layout->addWidget(m_sidebar);
    layout->addWidget(m_pageStack, 1);

    setCentralWidget(centralWidget);

    connect(m_sidebar, &Sidebar::toolSelected, this, [this](const QString& toolId) {
        if (m_pageManager) {
            auto* page = m_pageManager->currentToolId().isEmpty() ? nullptr : m_pageStack->currentWidget();
            m_pageManager->openTool(toolId);

            auto* newPage = m_pageStack->currentWidget();
            if (newPage && newPage != page) {
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
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
}