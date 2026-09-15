#pragma once

#include <QApplication>
#include <memory>

class ToolRegistry;
class NavigationManager;
class ToolPageManager;
class AppServices;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    ToolRegistry& toolRegistry();
    NavigationManager& navigationManager();
    ToolPageManager& pageManager();
    AppServices& services();

private:
    void registerTools();
    void setupNavigation();

    std::unique_ptr<AppServices> m_services;
    std::unique_ptr<ToolRegistry> m_registry;
    std::unique_ptr<NavigationManager> m_navigation;
    std::unique_ptr<ToolPageManager> m_pageManager;
};
