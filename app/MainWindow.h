#pragma once
#include <ElaWindow.h>
#include <QMap>
class AppSettings;
class ToolRegistry;
class NavigationManager;
class LazyToolPage;
class SettingsPage;
class MainWindow : public ElaWindow
{
    Q_OBJECT
public:
    MainWindow(AppSettings& settings, const ToolRegistry& tools, const NavigationManager& navigation, QWidget* parent = nullptr);
    QString pageKey(const QString& id) const;
    QString groupKey(const QString& id) const;
protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    void retranslate();
    AppSettings& m_settings;
    QMap<QString, QString> m_pageKeys;
    QMap<QString, QString> m_groupKeys;
    QMap<QString, QString> m_groupNames;
    QMap<QString, QString> m_toolNames;
    SettingsPage* m_settingsPage;
};
