#include "AppSettings.h"
#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QLocale>
#include <QEvent>
#include <QTimer>

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
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        if (!followsSystemTheme()) return;
        const auto mode = systemTheme();
        if (mode == m_themeMode) return;
        m_themeMode = mode;
        emit themeChanged(mode);
    });
    qApp->installEventFilter(this);
}
AppSettings::~AppSettings() { if (qApp && m_translator) qApp->removeTranslator(m_translator.get()); }
ElaThemeType::ThemeMode AppSettings::themeMode() const { return m_themeMode; }
bool AppSettings::followsSystemTheme() const
{
    const auto choice = m_store->value("Appearance/theme").toString();
    return choice != "light" && choice != "dark";
}
void AppSettings::setThemeMode(ElaThemeType::ThemeMode mode)
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
    const auto locale = preference == "system" ? localeForUiLanguages(QLocale::system().uiLanguages()) : preference;
    if (!applyLocale(locale)) return false;
    m_store->setValue("Appearance/locale", preference);
    emit localeChanged();
    return true;
}
bool AppSettings::applyLocale(const QString& locale)
{
    if (locale == m_locale) return true;
    // Load before removing the active translation, preserving it on failure.
    auto candidate = std::make_unique<QTranslator>();
    const auto path = QString(":/i18n/%1.qm").arg(locale);
    if (locale != "en" && !candidate->load(path)) return false;
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
    const auto locale = localeForUiLanguages(QLocale::system().uiLanguages());
    if (locale != m_locale && applyLocale(locale)) emit localeChanged();
}
bool AppSettings::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::LocaleChange)
        QTimer::singleShot(0, this, &AppSettings::refreshSystemLocale);
    return QObject::eventFilter(watched, event);
}
QByteArray AppSettings::windowGeometry() const { return m_store->value("Window/geometry").toByteArray(); }
void AppSettings::setWindowGeometry(const QByteArray& value) { m_store->setValue("Window/geometry", value); }
QString AppSettings::lastPage() const { return m_store->value("Window/page", "encoding.base64").toString(); }
void AppSettings::setLastPage(const QString& value) { m_store->setValue("Window/page", value); }
bool AppSettings::navigationExpanded() const { return m_store->value("Window/navigationExpanded", true).toBool(); }
void AppSettings::setNavigationExpanded(bool value) { m_store->setValue("Window/navigationExpanded", value); }
void AppSettings::sync() { m_store->sync(); }
