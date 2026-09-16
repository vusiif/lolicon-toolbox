#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QPushButton;
class QVariantAnimation;
class NavigationManager;
class ToolRegistry;

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
    void expandStateChanged(bool expanded);

private:
    void onItemClicked(QTreeWidgetItem* item, int column);

    QVBoxLayout* m_layout = nullptr;
    QPushButton* m_toggleBtn = nullptr;
    QTreeWidget* m_tree = nullptr;
    QWidget* m_bottomArea = nullptr;
    QVariantAnimation* m_widthAnim = nullptr;
    bool m_expanded = true;
    int m_expandedWidth = 220;
    int m_collapsedWidth = 56;
};