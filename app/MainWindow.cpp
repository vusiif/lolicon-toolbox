#include "MainWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
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

    auto* placeholder = new QLabel("Select a tool from the sidebar", this);
    placeholder->setAlignment(Qt::AlignCenter);

    m_pageStack = new QStackedWidget(this);
    m_pageStack->addWidget(placeholder);

    layout->addWidget(m_pageStack, 1);

    setCentralWidget(centralWidget);
}
