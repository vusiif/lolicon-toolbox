#pragma once

#include <QString>
#include "ThemeManager.h"

namespace theme
{
    inline QString WindowBg()       { return ThemeManager::instance().color("WindowBg"); }
    inline QString Surface0()       { return ThemeManager::instance().color("Surface0"); }
    inline QString Surface1()       { return ThemeManager::instance().color("Surface1"); }
    inline QString Surface2()       { return ThemeManager::instance().color("Surface2"); }
    inline QString Surface3()       { return ThemeManager::instance().color("Surface3"); }
    inline QString Border()         { return ThemeManager::instance().color("Border"); }
    inline QString TextPrimary()    { return ThemeManager::instance().color("TextPrimary"); }
    inline QString TextSecondary()  { return ThemeManager::instance().color("TextSecondary"); }
    inline QString TextDisabled()   { return ThemeManager::instance().color("TextDisabled"); }
    inline QString Accent()         { return ThemeManager::instance().color("Accent"); }
    inline QString AccentHover()    { return ThemeManager::instance().color("AccentHover"); }
    inline QString AccentPressed()  { return ThemeManager::instance().color("AccentPressed"); }
    inline QString Danger()         { return ThemeManager::instance().color("Danger"); }
    inline QString Success()        { return ThemeManager::instance().color("Success"); }
    inline QString SidebarBg()      { return ThemeManager::instance().color("SidebarBg"); }
    inline QString SidebarHover()   { return ThemeManager::instance().color("SidebarHover"); }
    inline QString SidebarActive()  { return ThemeManager::instance().color("SidebarActive"); }
    inline QString InputBg()        { return ThemeManager::instance().color("InputBg"); }
    inline QString InputBorder()    { return ThemeManager::instance().color("InputBorder"); }
    inline QString InputFocus()     { return ThemeManager::instance().color("InputFocus"); }
    inline QString ButtonBg()       { return ThemeManager::instance().color("ButtonBg"); }
    inline QString ButtonHover()    { return ThemeManager::instance().color("ButtonHover"); }
    inline QString ButtonPressed()  { return ThemeManager::instance().color("ButtonPressed"); }
    inline QString ScrollbarBg()    { return ThemeManager::instance().color("ScrollbarBg"); }
    inline QString ScrollbarThumb() { return ThemeManager::instance().color("ScrollbarThumb"); }

    QString globalStyleSheet();
}
