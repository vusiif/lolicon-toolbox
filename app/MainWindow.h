#pragma once

#include <QMainWindow>

class QStackedWidget;
class Sidebar;
class NavigationManager;
class ToolRegistry;
class ToolPageManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(NavigationManager* navManager,
                        ToolRegistry* registry,
                        ToolPageManager* pageManager,
                        QWidget* parent = nullptr);
    ~MainWindow() override = default;

private:
    void setupUi();

    QStackedWidget* m_pageStack = nullptr;
    Sidebar* m_sidebar = nullptr;
    NavigationManager* m_navManager = nullptr;
    ToolRegistry* m_registry = nullptr;
    ToolPageManager* m_pageManager = nullptr;
};
