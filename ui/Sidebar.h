#pragma once

#include <QWidget>
#include <QList>

class QTreeWidgetItem;
class QVBoxLayout;
class QPushButton;
class QLabel;
class QVariantAnimation;
class NavigationManager;
class ToolRegistry;

struct SidebarToolItem
{
    QString toolId;
    QString icon;
    QString name;
};

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void build(NavigationManager* navManager, ToolRegistry* registry);

    QWidget* bottomArea() const;

    void expand();
    void retract();
    void toggle();

    bool isExpanded() const;

signals:
    void toolSelected(const QString& toolId);
    void settingsClicked();
    void expandStateChanged(bool expanded);

private:
    void rebuildItems();
    void onItemClicked(int index);

    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_headerWidget = nullptr;
    QPushButton* m_toggleBtn = nullptr;
    QWidget* m_listWidget = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QWidget* m_bottomArea = nullptr;
    QLabel* m_versionLabel = nullptr;
    QVariantAnimation* m_widthAnim = nullptr;

    QList<SidebarToolItem> m_items;
    bool m_expanded = true;
    int m_expandedWidth = 220;
    int m_collapsedWidth = 56;
    int m_selectedIndex = -1;
};
