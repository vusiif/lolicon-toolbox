#pragma once

#include <QObject>
#include <QString>
#include <QMap>

struct ThemePreset {
    QString name;
    QMap<QString, QString> colors;
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager& instance();

    void applyPreset(const QString& presetName);
    void setColor(const QString& key, const QString& value);
    QString color(const QString& key) const;
    QStringList presetNames() const;
    QString currentPresetName() const;

    QString styleSheet() const;

signals:
    void themeChanged();

private:
    ThemeManager();
    void initPresets();
    void applyColors(const QMap<QString, QString>& colors);

    QMap<QString, ThemePreset> m_presets;
    QString m_currentPreset;
};
