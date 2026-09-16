#include "LanguageManager.h"
#include <QTranslator>
#include <QApplication>
#include <QDir>
#include <QFileInfo>

LanguageManager& LanguageManager::instance()
{
    static LanguageManager inst;
    return inst;
}

LanguageManager::LanguageManager()
    : m_translator(new QTranslator(this))
{
    m_localeNames["en"] = "English";
    m_localeNames["zh_CN"] = QString::fromUtf8("\u7B80\u4F53\u4E2D\u6587");
    m_localeNames["zh_TW"] = QString::fromUtf8("\u7E41\u9AD4\u4E2D\u6587");
}

void LanguageManager::setLanguage(const QString& locale)
{
    if (locale == m_currentLocale) return;

    m_currentLocale = locale;
    loadTranslation(locale);
    emit languageChanged();
}

QString LanguageManager::currentLocale() const
{
    return m_currentLocale;
}

QStringList LanguageManager::availableLocales() const
{
    return m_localeNames.keys();
}

QString LanguageManager::localeName(const QString& locale) const
{
    auto it = m_localeNames.find(locale);
    return it != m_localeNames.end() ? it.value() : locale;
}

void LanguageManager::loadTranslation(const QString& locale)
{
    qApp->removeTranslator(m_translator);

    if (locale == "en") {
        return;
    }

    QString qmPath = QCoreApplication::applicationDirPath()
                     + "/translations/" + locale + ".qm";

    if (QFileInfo::exists(qmPath)) {
        m_translator->load(qmPath);
        qApp->installTranslator(m_translator);
    }
}
