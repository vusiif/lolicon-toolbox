#include "Application.h"
#include "MainWindow.h"
#include "services/AppSettings.h"
#include "tool/ToolRegistry.h"
#include "navigation/NavigationManager.h"
#include "Base64Page.h"
#include "MojibakePage.h"
#include "UiTheme.h"
#include <ElaApplication.h>
#include <ElaTheme.h>
Application::Application(int& argc, char** argv) : QApplication(argc, argv)
{
    setApplicationName("Toolbox");
    setApplicationVersion("0.2.0");
    eApp->init();
    m_settings = std::make_unique<AppSettings>();
    eTheme->setThemeMode(m_settings->themeMode());
    ui::initializeTheme(this);
    m_tools = std::make_unique<ToolRegistry>();
    ToolDescriptor base64;
    base64.id = "encoding.base64";
    base64.name = "Base64";
    base64.icon = ElaIconType::Code;
    base64.factory = [](QWidget* parent) { return new Base64Page(parent); };
    m_tools->registerTool(base64);
    ToolDescriptor mojibake;
    mojibake.id = "encoding.mojibake";
    mojibake.name = "Mojibake Repair";
    mojibake.icon = ElaIconType::Language;
    mojibake.factory = [](QWidget* parent) { return new MojibakePage(parent); };
    m_tools->registerTool(mojibake);
    NavigationManager navigation;
    navigation.addGroup({"encoding", QT_TRANSLATE_NOOP("Navigation", "Text and Encoding"), 10});
    navigation.addItem({"encoding.base64", "encoding", 10, true});
    navigation.addItem({"encoding.mojibake", "encoding", 20, true});
    m_window = std::make_unique<MainWindow>(*m_settings, *m_tools, navigation);
    m_window->show();
}
Application::~Application() = default;
