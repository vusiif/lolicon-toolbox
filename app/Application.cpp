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
    navigation.addGroup({"encoding", QT_TRANSLATE_NOOP("Navigation", "Text and Encoding"), 10});
    navigation.addItem({"encoding.base64", "encoding", 10, true});
    navigation.addItem({"encoding.mojibake", "encoding", 20, true});
    navigation.addItem({"text.style_converter", "encoding", 30, true});
    navigation.addGroup({"speech", QT_TRANSLATE_NOOP("Navigation", "Audio and Speech"), 20});
    navigation.addItem({"speech.voice_designer", "speech", 10, true});
    navigation.addGroup({"image", QT_TRANSLATE_NOOP("Navigation", "Image Tools"), 30});
    navigation.addItem({"image.character_art", "image", 10, true});
    m_window = std::make_unique<MainWindow>(*m_settings, *m_tools, navigation);
    m_window->show();
}
Application::~Application() = default;
