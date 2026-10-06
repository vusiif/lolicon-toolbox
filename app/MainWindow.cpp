#include "MainWindow.h"
#include "services/AppSettings.h"
#include "tool/ToolRegistry.h"
#include "navigation/NavigationManager.h"
#include "LazyToolPage.h"
#include "SettingsPage.h"
#include <ElaTheme.h>
#include <ElaNavigationBar.h>
#include <QHBoxLayout>
#include <QEvent>
#include <QCloseEvent>
#include <QStyle>

MainWindow::MainWindow(AppSettings& settings, const ToolRegistry& tools, const NavigationManager& navigationConfig, QWidget* parent)
    : ElaWindow(parent), m_settings(settings)
{
    setObjectName("toolboxWindow");
    setWindowTitle("Toolbox");
    setWindowIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    resize(1000, 700);
    setMinimumSize(640, 480);
    setUserInfoCardVisible(false);
    setIsAllowPageOpenInNewWindow(false);
    setNavigationBarWidth(220);
    // Populate/expand groups in full mode; Compact expansion opens popup menus.
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setStackSwitchMode(ElaWindowType::None);
    setThemeChangeTime(180);
    setWindowButtonFlags(ElaAppBarType::NavigationButtonHint | ElaAppBarType::ThemeChangeButtonHint
        | ElaAppBarType::StayTopButtonHint | ElaAppBarType::MinimizeButtonHint
        | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
    // Adapt the two utility positions; Ela retains drag, resize and window controls.
    auto* bar = findChild<ElaAppBar*>();
    auto* box = qobject_cast<QHBoxLayout*>(bar->layout());
    auto* right = qobject_cast<QHBoxLayout*>(box->itemAt(box->count() - 1)->layout());
    auto* pin = right->takeAt(0)->layout();
    right->insertLayout(1, pin);
    connect(eTheme, &ElaTheme::themeModeChanged, &settings, [&settings](ElaThemeType::ThemeMode mode) {
        // System-driven changes already updated settings; only user toggles are overrides.
        if (mode != settings.themeMode()) settings.setThemeMode(mode);
    });
    connect(&settings, &AppSettings::themeChanged, this, [](ElaThemeType::ThemeMode mode) {
        if (eTheme->getThemeMode() != mode) eTheme->setThemeMode(mode);
    });
    QString firstPageKey;
    for (const auto& section : navigationConfig.sections(tools)) {
        QString groupKey;
        const auto title = QCoreApplication::translate("Navigation", section.group.name.toUtf8().constData());
        if (addExpanderNode(title, groupKey, ElaIconType::Folder) != ElaNavigationType::Success) continue;
        m_groupKeys.insert(section.group.id, groupKey);
        m_groupNames.insert(section.group.id, section.group.name);
        for (const auto& item : section.items) {
            const auto tool = tools.findTool(item.toolId);
            if (!tool) continue;
            auto* page = new LazyToolPage(*tool, this);
            page->setObjectName(tool->id);
            const auto toolTitle = QCoreApplication::translate("Tools", tool->name.toUtf8().constData());
            if (addPageNode(toolTitle, page, groupKey, tool->icon) == ElaNavigationType::Success) {
                const auto key = page->property("ElaPageKey").toString();
                m_pageKeys.insert(tool->id, key);
                m_toolNames.insert(tool->id, tool->name);
                if (firstPageKey.isEmpty()) firstPageKey = key;
            } else delete page;
        }
        expandNavigationNode(groupKey);
    }
    m_settingsPage = new SettingsPage(settings, this);
    QString key;
    addFooterNode(tr("Settings"), m_settingsPage, key, 0, ElaIconType::Gear);
    m_pageKeys.insert("settings", key);
    connect(this, &ElaWindow::navigationNodeClicked, this, [this](ElaNavigationType::NavigationNodeType, const QString& key) {
        const auto id = m_pageKeys.key(key);
        if (!id.isEmpty()) m_settings.setLastPage(id);
    });
    const auto savedGeometry = settings.windowGeometry();
    if (!savedGeometry.isEmpty()) restoreGeometry(savedGeometry);
    auto savedKey = pageKey(settings.lastPage());
    if (savedKey.isEmpty()) savedKey = firstPageKey.isEmpty() ? pageKey("settings") : firstPageKey;
    if (!savedKey.isEmpty()) {
        navigation(savedKey);
        // Ela may already select the first inserted page without emitting a click.
        m_settings.setLastPage(m_pageKeys.key(savedKey));
    }
    setNavigationBarDisplayMode(settings.navigationExpanded() ? ElaNavigationType::Maximal : ElaNavigationType::Compact);
}
QString MainWindow::pageKey(const QString& id) const { return m_pageKeys.value(id); }
QString MainWindow::groupKey(const QString& id) const { return m_groupKeys.value(id); }
void MainWindow::retranslate()
{
    setNavigationNodeTitle(pageKey("settings"), tr("Settings"));
    for (auto it = m_groupNames.cbegin(); it != m_groupNames.cend(); ++it)
        setNavigationNodeTitle(groupKey(it.key()), QCoreApplication::translate("Navigation", it.value().toUtf8().constData()));
    for (auto it = m_toolNames.cbegin(); it != m_toolNames.cend(); ++it)
        setNavigationNodeTitle(pageKey(it.key()), QCoreApplication::translate("Tools", it.value().toUtf8().constData()));
}
void MainWindow::changeEvent(QEvent* event)
{
    ElaWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    m_settings.setWindowGeometry(saveGeometry());
    const auto* nav = findChild<ElaNavigationBar*>();
    if (nav) m_settings.setNavigationExpanded(nav->getDisplayMode() == ElaNavigationType::Maximal);
    m_settings.sync();
    ElaWindow::closeEvent(event);
}
