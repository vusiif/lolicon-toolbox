#include "Application.h"
#include "MainWindow.h"
#include "services/AppSettings.h"
#include "tool/ToolRegistry.h"
#include "navigation/NavigationManager.h"
#include "Base64Page.h"
#include "MojibakePage.h"
#include "TextStylePage.h"
#include "VoiceDesignerPage.h"
#include "CharacterArtPage.h"
#include "UiTheme.h"
#include <ElaApplication.h>
#include <ElaTheme.h>
Application::Application(int& argc, char** argv) : QApplication(argc, argv)
{
    setApplicationName("Toolbox");
    setApplicationVersion(TOOLBOX_VERSION);
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
    mojibake.factory = [this](QWidget* parent) { return new MojibakePage(parent, m_settings.get()); };
    m_tools->registerTool(mojibake);
    ToolDescriptor textStyle;
    textStyle.id = "text.style_converter"; textStyle.name = "Text Style Converter"; textStyle.icon = ElaIconType::Language;
    textStyle.factory = [this](QWidget* parent) { return new TextStylePage(parent, m_settings.get()); };
    m_tools->registerTool(textStyle);
    ToolDescriptor voice;
    voice.id = "speech.voice_designer"; voice.name = "Voice Designer"; voice.icon = ElaIconType::Microphone;
    voice.factory = [this](QWidget* parent) { return new VoiceDesignerPage(parent, m_settings.get()); };
    m_tools->registerTool(voice);
    ToolDescriptor art;
    art.id = "image.character_art"; art.name = "Character Art Generator"; art.icon = ElaIconType::Image;
    art.factory = [](QWidget* parent) { return new CharacterArtPage(parent); };
    m_tools->registerTool(art);
    NavigationManager navigation;
    navigation.addGroup({.id = "encoding", .name = QT_TRANSLATE_NOOP("Navigation", "Text and Encoding"), .order = 10});
    navigation.addItem({.toolId = "encoding.base64", .groupId = "encoding", .order = 10, .visible = true});
    navigation.addItem({.toolId = "encoding.mojibake", .groupId = "encoding", .order = 20, .visible = true});
    navigation.addItem({.toolId = "text.style_converter", .groupId = "encoding", .order = 30, .visible = true});
    navigation.addGroup({.id = "speech", .name = QT_TRANSLATE_NOOP("Navigation", "Audio and Speech"), .order = 20});
    navigation.addItem({.toolId = "speech.voice_designer", .groupId = "speech", .order = 10, .visible = true});
    navigation.addGroup({.id = "image", .name = QT_TRANSLATE_NOOP("Navigation", "Image Tools"), .order = 30});
    navigation.addItem({.toolId = "image.character_art", .groupId = "image", .order = 10, .visible = true});
    m_window = std::make_unique<MainWindow>(*m_settings, *m_tools, navigation);
    m_window->show();
}
Application::~Application() = default;
