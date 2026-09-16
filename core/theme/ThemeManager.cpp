#include "ThemeManager.h"

ThemeManager& ThemeManager::instance()
{
    static ThemeManager inst;
    return inst;
}

ThemeManager::ThemeManager()
{
    initPresets();
    applyPreset("Midnight Blue");
}

void ThemeManager::initPresets()
{
    {
        ThemePreset p;
        p.name = "Midnight Blue";
        p.colors = {
            {"WindowBg",       "#1A2034"},
            {"Surface0",       "#1E2538"},
            {"Surface1",       "#242B3D"},
            {"Surface2",       "#2A3246"},
            {"Surface3",       "#333B50"},
            {"Border",         "#3A4258"},
            {"TextPrimary",    "#E0E4F0"},
            {"TextSecondary",  "#8B93A8"},
            {"TextDisabled",   "#5A6278"},
            {"Accent",         "#7B8CDE"},
            {"AccentHover",    "#9BA8E8"},
            {"AccentPressed",  "#6A7BD0"},
            {"Danger",         "#E06080"},
            {"Success",        "#70C0A0"},
            {"SidebarBg",      "#151B2C"},
            {"SidebarHover",   "#1E2538"},
            {"SidebarActive",  "#242B3D"},
            {"InputBg",        "#1E2538"},
            {"InputBorder",    "#2A3246"},
            {"InputFocus",     "#7B8CDE"},
            {"ButtonBg",       "#2A3246"},
            {"ButtonHover",    "#333B50"},
            {"ButtonPressed",  "#1E2538"},
            {"ScrollbarBg",    "#1A2034"},
            {"ScrollbarThumb", "#3A4258"},
        };
        m_presets[p.name] = p;
    }
    {
        ThemePreset p;
        p.name = "Dracula";
        p.colors = {
            {"WindowBg",       "#282A36"},
            {"Surface0",       "#2D303E"},
            {"Surface1",       "#343746"},
            {"Surface2",       "#3E4155"},
            {"Surface3",       "#474A5B"},
            {"Border",         "#525568"},
            {"TextPrimary",    "#F8F8F2"},
            {"TextSecondary",  "#BFBFBF"},
            {"TextDisabled",   "#6272A4"},
            {"Accent",         "#BD93F9"},
            {"AccentHover",    "#CAA8FF"},
            {"AccentPressed",  "#A577F0"},
            {"Danger",         "#FF5555"},
            {"Success",        "#50FA7B"},
            {"SidebarBg",      "#21222C"},
            {"SidebarHover",   "#2D303E"},
            {"SidebarActive",  "#343746"},
            {"InputBg",        "#2D303E"},
            {"InputBorder",    "#3E4155"},
            {"InputFocus",     "#BD93F9"},
            {"ButtonBg",       "#3E4155"},
            {"ButtonHover",    "#474A5B"},
            {"ButtonPressed",  "#2D303E"},
            {"ScrollbarBg",    "#282A36"},
            {"ScrollbarThumb", "#525568"},
        };
        m_presets[p.name] = p;
    }
    {
        ThemePreset p;
        p.name = "Gruvbox Dark";
        p.colors = {
            {"WindowBg",       "#282828"},
            {"Surface0",       "#303030"},
            {"Surface1",       "#383838"},
            {"Surface2",       "#404040"},
            {"Surface3",       "#4E4E4E"},
            {"Border",         "#585858"},
            {"TextPrimary",    "#EBDBB2"},
            {"TextSecondary",  "#A89984"},
            {"TextDisabled",   "#665C54"},
            {"Accent",         "#FE8019"},
            {"AccentHover",    "#FF9E3D"},
            {"AccentPressed",  "#E07000"},
            {"Danger",         "#FB4934"},
            {"Success",        "#B8BB26"},
            {"SidebarBg",      "#1D2021"},
            {"SidebarHover",   "#303030"},
            {"SidebarActive",  "#383838"},
            {"InputBg",        "#303030"},
            {"InputBorder",    "#404040"},
            {"InputFocus",     "#FE8019"},
            {"ButtonBg",       "#404040"},
            {"ButtonHover",    "#4E4E4E"},
            {"ButtonPressed",  "#303030"},
            {"ScrollbarBg",    "#282828"},
            {"ScrollbarThumb", "#585858"},
        };
        m_presets[p.name] = p;
    }
    {
        ThemePreset p;
        p.name = "Solarized Light";
        p.colors = {
            {"WindowBg",       "#FDF6E3"},
            {"Surface0",       "#EEE8D5"},
            {"Surface1",       "#E6DFCA"},
            {"Surface2",       "#DDD6C1"},
            {"Surface3",       "#D3CCB8"},
            {"Border",         "#BFB8A0"},
            {"TextPrimary",    "#657B83"},
            {"TextSecondary",  "#839496"},
            {"TextDisabled",   "#93A1A1"},
            {"Accent",         "#268BD2"},
            {"AccentHover",    "#4DA3E0"},
            {"AccentPressed",  "#1A6FB0"},
            {"Danger",         "#DC322F"},
            {"Success",        "#859900"},
            {"SidebarBg",      "#EEE8D5"},
            {"SidebarHover",   "#E6DFCA"},
            {"SidebarActive",  "#DDD6C1"},
            {"InputBg",        "#FDF6E3"},
            {"InputBorder",    "#BFB8A0"},
            {"InputFocus",     "#268BD2"},
            {"ButtonBg",       "#DDD6C1"},
            {"ButtonHover",    "#D3CCB8"},
            {"ButtonPressed",  "#EEE8D5"},
            {"ScrollbarBg",    "#FDF6E3"},
            {"ScrollbarThumb", "#BFB8A0"},
        };
        m_presets[p.name] = p;
    }
}

void ThemeManager::applyPreset(const QString& presetName)
{
    auto it = m_presets.find(presetName);
    if (it == m_presets.end()) return;

    m_currentPreset = presetName;
    applyColors(it->colors);
    emit themeChanged();
}

void ThemeManager::setColor(const QString& key, const QString& value)
{
    if (m_presets.contains(m_currentPreset)) {
        m_presets[m_currentPreset].colors[key] = value;
    }
    emit themeChanged();
}

QString ThemeManager::color(const QString& key) const
{
    auto it = m_presets.find(m_currentPreset);
    if (it != m_presets.end()) {
        auto cit = it->colors.find(key);
        if (cit != it->colors.end()) return cit.value();
    }
    return "#000000";
}

QStringList ThemeManager::presetNames() const
{
    return m_presets.keys();
}

QString ThemeManager::currentPresetName() const
{
    return m_currentPreset;
}

QString ThemeManager::styleSheet() const
{
    return QStringLiteral(R"(
        * {
            font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
        }
        QMainWindow {
            background-color: %1;
        }
        QWidget#centralWidget {
            background-color: %1;
        }
    )").arg(color("WindowBg"));
}

void ThemeManager::applyColors(const QMap<QString, QString>& colors)
{
    Q_UNUSED(colors);
}
