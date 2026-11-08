#pragma once
#include <QObject>
#include <QTranslator>
#include <QStringList>
#include <memory>
#include <ElaWidgetToolsDef.h>
#include "ai/AiProvider.h"
class QSettings;

class AppSettings : public QObject
{
    Q_OBJECT
public:
    explicit AppSettings(const QString& iniPath = {}, QObject* parent = nullptr);
    ~AppSettings() override;
    ElaThemeType::ThemeMode themeMode() const;
    void setThemeMode(ElaThemeType::ThemeMode mode);
    bool followsSystemTheme() const;
    QString locale() const { return m_locale; }
    QString localePreference() const;
    static QString localeForUiLanguages(const QStringList& languages);
    bool setLocale(const QString& locale);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    QString lastPage() const;
    void setLastPage(const QString& id);
    bool navigationExpanded() const;
    void setNavigationExpanded(bool expanded);
    void sync();
    ai::Configuration aiConfiguration() const;
    bool setAiConfiguration(const ai::Configuration& config);
    // Audio API used by Voice Designer; stored separately from the text AI API.
    ai::Configuration voiceConfiguration() const;
    bool setVoiceConfiguration(const ai::Configuration& config);
    bool hasVoiceConfiguration() const;
    static QString defaultVoiceBaseUrl();
    static QString defaultVoiceModel();
    QString textStylePreset() const;
    void setTextStylePreset(const QString& preset);
signals:
    void themeChanged(ElaThemeType::ThemeMode mode);
    void localeChanged();
    void aiConfigurationChanged();
    void voiceConfigurationChanged();
private:
    bool storeConfiguration(const QString& group, const ai::Configuration& config);
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool applyLocale(const QString& locale);
    void refreshSystemLocale();
    std::unique_ptr<QSettings> m_store;
    std::unique_ptr<QTranslator> m_translator;
    QString m_locale = "en";
    ElaThemeType::ThemeMode m_themeMode = ElaThemeType::Light;
};
