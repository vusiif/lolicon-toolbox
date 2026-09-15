#include "AppServices.h"

AppServices::AppServices()
{
}

SettingsService& AppServices::settings()
{
    return m_settings;
}
