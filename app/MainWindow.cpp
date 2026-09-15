#include "MainWindow.h"
#include "Sidebar.h"
#include "navigation/NavigationManager.h"
#include "tool/ToolRegistry.h"
#include "tool/ToolPageManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QWidget>

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
    resize(900, 600);
}

void MainWindow::setupUi()
{
    auto* centralWidget = new QWidget(this);
    auto* layout = new QHBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_sidebar = new Sidebar(this);
    m_sidebar->build(m_navManager, m_registry);
    m_sidebar->setFixedWidth(200);

    auto* placeholder = new QLabel("Select a tool from the sidebar", this);
    placeholder->setAlignment(Qt::AlignCenter);

    m_pageStack = new QStackedWidget(this);
    m_pageStack->addWidget(placeholder);

    layout->addWidget(m_sidebar);
    layout->addWidget(m_pageStack, 1);

    setCentralWidget(centralWidget);

    connect(m_sidebar, &Sidebar::toolSelected, this, [this](const QString& toolId) {
        if (m_pageManager) {
            m_pageManager->openTool(toolId);
        }
    });
}
