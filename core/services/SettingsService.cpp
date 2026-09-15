#include "SettingsService.h"

SettingsService::SettingsService()
    : m_settings("Toolbox", "Toolbox")
{
}

QByteArray SettingsService::mainWindowGeometry() const
{
    return m_settings.value("MainWindow/geometry").toByteArray();
}

void SettingsService::setMainWindowGeometry(const QByteArray& geometry)
{
    m_settings.setValue("MainWindow/geometry", geometry);
}

QString SettingsService::lastToolId() const
{
    return m_settings.value("MainWindow/lastToolId").toString();
}

void SettingsService::setLastToolId(const QString& toolId)
{
    m_settings.setValue("MainWindow/lastToolId", toolId);
}
