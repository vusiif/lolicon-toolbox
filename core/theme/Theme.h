#pragma once

#include <QColor>
#include <QString>

namespace theme
{
    constexpr const char* WindowBg       = "#1A2034";
    constexpr const char* Surface0       = "#1E2538";
    constexpr const char* Surface1       = "#242B3D";
    constexpr const char* Surface2       = "#2A3246";
    constexpr const char* Surface3       = "#333B50";
    constexpr const char* Border         = "#3A4258";
    constexpr const char* TextPrimary    = "#E0E4F0";
    constexpr const char* TextSecondary  = "#8B93A8";
    constexpr const char* TextDisabled   = "#5A6278";
    constexpr const char* Accent         = "#7B8CDE";
    constexpr const char* AccentHover    = "#9BA8E8";
    constexpr const char* AccentPressed  = "#6A7BD0";
    constexpr const char* Danger         = "#E06080";
    constexpr const char* Success        = "#70C0A0";
    constexpr const char* SidebarBg      = "#151B2C";
    constexpr const char* SidebarHover   = "#1E2538";
    constexpr const char* SidebarActive  = "#242B3D";
    constexpr const char* InputBg        = "#1E2538";
    constexpr const char* InputBorder    = "#2A3246";
    constexpr const char* InputFocus     = "#7B8CDE";
    constexpr const char* ButtonBg       = "#2A3246";
    constexpr const char* ButtonHover    = "#333B50";
    constexpr const char* ButtonPressed  = "#1E2538";
    constexpr const char* ScrollbarBg    = "#1A2034";
    constexpr const char* ScrollbarThumb = "#3A4258";

    QString globalStyleSheet();
}