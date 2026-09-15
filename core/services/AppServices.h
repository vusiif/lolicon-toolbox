#pragma once

#include "SettingsService.h"

class AppServices
{
public:
    AppServices();

    SettingsService& settings();

private:
    SettingsService m_settings;
};
