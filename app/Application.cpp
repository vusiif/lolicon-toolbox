#include "Application.h"
#include "MainWindow.h"

#include "tool/ToolRegistry.h"
#include "tool/ToolPageManager.h"
#include "navigation/NavigationManager.h"
#include "services/AppServices.h"

#include "Base64Page.h"

#include <QStackedWidget>

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
    , m_services(std::make_unique<AppServices>())
    , m_registry(std::make_unique<ToolRegistry>())
    , m_navigation(std::make_unique<NavigationManager>())
{
    setApplicationName("Toolbox");
    setApplicationVersion("0.1.0");

    registerTools();
    setupNavigation();

    auto* stack = new QStackedWidget();
    m_pageManager = std::make_unique<ToolPageManager>(stack, *m_registry);

    auto* window = new MainWindow(m_navigation.get(), m_registry.get(), m_pageManager.get());
    window->show();

    auto lastTool = m_services->settings().lastToolId();
    if (!lastTool.isEmpty() && m_registry->contains(lastTool)) {
        m_pageManager->openTool(lastTool);
    }
}

Application::~Application() = default;

ToolRegistry& Application::toolRegistry()
{
    return *m_registry;
}

NavigationManager& Application::navigationManager()
{
    return *m_navigation;
}

ToolPageManager& Application::pageManager()
{
    return *m_pageManager;
}

AppServices& Application::services()
{
    return *m_services;
}

void Application::registerTools()
{
    ToolDescriptor base64;
    base64.id = "encoding.base64";
    base64.name = "Base64";
    base64.description = "Encode and decode Base64 text";
    base64.factory = [](QWidget* parent) -> ToolPage* {
        return new Base64Page(parent);
    };
    m_registry->registerTool(base64);
}

void Application::setupNavigation()
{
    NavigationGroup encoding;
    encoding.id = "encoding";
    encoding.name = "Text && Encoding";
    encoding.order = 200;
    m_navigation->registerGroup(encoding);

    NavigationItem base64Item;
    base64Item.toolId = "encoding.base64";
    base64Item.groupId = "encoding";
    base64Item.order = 100;
    base64Item.visible = true;
    m_navigation->registerItem(base64Item);
}
