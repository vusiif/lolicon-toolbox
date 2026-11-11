#include "AppSettings.h"
#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QLocale>
#include <QEvent>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace {
ElaThemeType::ThemeMode systemTheme()
{
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) return ElaThemeType::Dark;
    if (scheme == Qt::ColorScheme::Light) return ElaThemeType::Light;
#ifdef Q_OS_WIN
    const QSettings personalization("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", QSettings::NativeFormat);
    return personalization.value("AppsUseLightTheme", 1).toInt() == 0 ? ElaThemeType::Dark : ElaThemeType::Light;
#else
    return ElaThemeType::Light;
#endif
}
}
AppSettings::AppSettings(const QString& path, QObject* parent) : QObject(parent)
{
    m_store = path.isEmpty() ? std::make_unique<QSettings>("Toolbox", "Toolbox")
        : std::make_unique<QSettings>(path, QSettings::IniFormat);
    m_themeMode = followsSystemTheme() ? systemTheme()
        : m_store->value("Appearance/theme").toString() == "light" ? ElaThemeType::Light : ElaThemeType::Dark;
    const auto preference = localePreference();
    applyLocale(preference == "system" ? localeForUiLanguages(QLocale::system().uiLanguages()) : preference);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (!followsSystemTheme()) return;
        const auto mode = systemTheme();
        if (mode == m_themeMode) return;
        m_themeMode = mode;
        emit themeChanged(mode);
    });
    qApp->installEventFilter(this);
}
AppSettings::~AppSettings() { if (qApp && m_translator) qApp->removeTranslator(m_translator.get()); }
QString AppSettings::textStylePreset() const {
    const auto value = m_store->value("TextStyle/preset", "medium").toString();
    return value == "light" || value == "heavy" ? value : "medium";
}
void AppSettings::setTextStylePreset(const QString& preset) {
    if (preset != "light" && preset != "medium" && preset != "heavy") return;
    m_store->setValue("TextStyle/style", "mars_classic"); m_store->setValue("TextStyle/preset", preset); m_store->remove("TextStyle/advanced");
}
ElaThemeType::ThemeMode AppSettings::themeMode() const { return m_themeMode; }
bool AppSettings::followsSystemTheme() const
{
    const auto choice = m_store->value("Appearance/theme").toString();
    return choice != "light" && choice != "dark";
}
void AppSettings::setThemeMode(const ElaThemeType::ThemeMode mode)
{
    if (mode != ElaThemeType::Light && mode != ElaThemeType::Dark) return;
    m_store->setValue("Appearance/theme", mode == ElaThemeType::Light ? "light" : "dark");
    if (m_themeMode == mode) return;
    m_themeMode = mode;
    emit themeChanged(mode);
}
QString AppSettings::localePreference() const
{
    const auto choice = m_store->value("Appearance/locale", "system").toString();
    return choice == "en" || choice == "zh_CN" || choice == "zh_TW" ? choice : "system";
}
QString AppSettings::localeForUiLanguages(const QStringList& languages)
{
    for (const auto& tag : languages) {
        const QLocale locale(tag);
        if (locale.language() == QLocale::Chinese)
            return locale.script() == QLocale::TraditionalHanScript ? "zh_TW" : "zh_CN";
        if (locale.language() == QLocale::English) return "en";
    }
    return "en";
}
bool AppSettings::setLocale(const QString& preference)
{
    if (preference != "system" && preference != "en" && preference != "zh_CN" && preference != "zh_TW") return false;
    if (const auto locale = preference == "system" ? localeForUiLanguages(QLocale::system().uiLanguages()) : preference;
        !applyLocale(locale)) return false;
    m_store->setValue("Appearance/locale", preference);
    emit localeChanged();
    return true;
}
bool AppSettings::applyLocale(const QString& locale)
{
    if (locale == m_locale) return true;
    // Load before removing the active translation, preserving it on failure.
    auto candidate = std::make_unique<QTranslator>();
    if (const auto path = QString(":/i18n/%1.qm").arg(locale); locale != "en" && !candidate->load(path)) return false;
    if (qApp && m_translator) qApp->removeTranslator(m_translator.get());
    m_translator.reset();
    if (locale != "en") {
        m_translator = std::move(candidate);
        if (qApp) qApp->installTranslator(m_translator.get());
    }
    m_locale = locale;
    return true;
}
void AppSettings::refreshSystemLocale()
{
    if (localePreference() != "system") return;
    if (const auto locale = localeForUiLanguages(QLocale::system().uiLanguages()); locale != m_locale && applyLocale(locale))
        emit localeChanged();
}
bool AppSettings::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::LocaleChange)
        QTimer::singleShot(0, this, &AppSettings::refreshSystemLocale);
    return QObject::eventFilter(watched, event);
}
QByteArray AppSettings::windowGeometry() const { return m_store->value("Window/geometry").toByteArray(); }
void AppSettings::setWindowGeometry(const QByteArray& geometry) { m_store->setValue("Window/geometry", geometry); }
QString AppSettings::lastPage() const { return m_store->value("Window/page", "encoding.base64").toString(); }
void AppSettings::setLastPage(const QString& id) { m_store->setValue("Window/page", id); }
bool AppSettings::navigationExpanded() const { return m_store->value("Window/navigationExpanded", true).toBool(); }
void AppSettings::setNavigationExpanded(const bool expanded) { m_store->setValue("Window/navigationExpanded", expanded); }
void AppSettings::sync() { m_store->sync(); }
namespace {
QString unprotectKey(QByteArray encrypted) {
    QString key;
#ifdef Q_OS_WIN
    if (!encrypted.isEmpty()) {
        DATA_BLOB input{.cbData = static_cast<DWORD>(encrypted.size()), .pbData = reinterpret_cast<BYTE*>(encrypted.data())}, output{};
        if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            key = QString::fromUtf8(reinterpret_cast<char*>(output.pbData), output.cbData);
            SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
        }
    }
#endif
    return key;
}
// Empty key -> empty result. Returns false when a nonempty key cannot be protected.
bool protectKey(const QString& key, QByteArray& encrypted) {
    encrypted.clear();
    if (key.isEmpty()) return true;
#ifdef Q_OS_WIN
    auto bytes = key.toUtf8();
    DATA_BLOB input{.cbData = static_cast<DWORD>(bytes.size()), .pbData = reinterpret_cast<BYTE*>(bytes.data())}, output{};
    const auto success = CryptProtectData(&input, L"Toolbox API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(bytes.data(), bytes.size());
    if (!success) return false;
    encrypted = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
#else
    return false; // Never fall back to storing plaintext credentials.
#endif
}
}
ai::Configuration AppSettings::aiConfiguration() const {
    ai::Configuration config;
    config.baseUrl = m_store->value("AI/baseUrl", "https://api.openai.com/v1").toString();
    config.model = m_store->value("AI/model").toString();
    config.apiKey = unprotectKey(m_store->value("AI/protectedKey").toByteArray());
    return config;
}
bool AppSettings::storeConfiguration(const QString& group, const ai::Configuration& config) {
    QByteArray encrypted;
    if (!protectKey(config.apiKey, encrypted)) return false;
    m_store->setValue(group + "/baseUrl", config.baseUrl.trimmed());
    m_store->setValue(group + "/model", config.model.trimmed());
    if (encrypted.isEmpty()) m_store->remove(group + "/protectedKey");
    else m_store->setValue(group + "/protectedKey", encrypted);
    m_store->sync();
    return m_store->status() == QSettings::NoError;
}
bool AppSettings::setAiConfiguration(const ai::Configuration& config) {
    if (!storeConfiguration("AI", config)) return false;
    emit aiConfigurationChanged();
    return true;
}
QString AppSettings::defaultVoiceBaseUrl() { return QStringLiteral("https://api.xiaomimimo.com/v1"); }
QString AppSettings::defaultVoiceModel() { return QStringLiteral("mimo-v2.5-tts-voicedesign"); }
bool AppSettings::hasVoiceConfiguration() const { return m_store->contains("Voice/baseUrl"); }
ai::Configuration AppSettings::voiceConfiguration() const {
    // Until the voice API is saved once, keep the pre-split behaviour of reusing the
    // text API address and key, but never its text model.
    if (!hasVoiceConfiguration()) {
        auto config = aiConfiguration();
        config.model = defaultVoiceModel();
        return config;
    }
    ai::Configuration config;
    config.baseUrl = m_store->value("Voice/baseUrl").toString();
    config.model = m_store->value("Voice/model", defaultVoiceModel()).toString();
    config.apiKey = unprotectKey(m_store->value("Voice/protectedKey").toByteArray());
    return config;
}
bool AppSettings::setVoiceConfiguration(const ai::Configuration& config) {
    if (!storeConfiguration("Voice", config)) return false;
    emit voiceConfigurationChanged();
    return true;
}
