#pragma once

#include <QSettings>
#include <QString>

class SettingsService
{
public:
    SettingsService();

    QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray& geometry);

    QString lastToolId() const;
    void setLastToolId(const QString& toolId);

private:
    QSettings m_settings;
};
