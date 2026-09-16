#pragma once

#include <QObject>
#include <QMap>
#include <QString>

class QTranslator;
class QApplication;

class LanguageManager : public QObject
{
    Q_OBJECT

public:
    static LanguageManager& instance();

    void setLanguage(const QString& locale);
    QString currentLocale() const;
    QStringList availableLocales() const;
    QString localeName(const QString& locale) const;

signals:
    void languageChanged();

private:
    LanguageManager();
    void loadTranslation(const QString& locale);

    QTranslator* m_translator = nullptr;
    QString m_currentLocale = "en";
    QMap<QString, QString> m_localeNames;
};
