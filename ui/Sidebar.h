#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class NavigationManager;
class ToolRegistry;

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void build(NavigationManager* navManager, ToolRegistry* registry);

signals:
    void toolSelected(const QString& toolId);

private:
    void onItemClicked(QTreeWidgetItem* item, int column);

    QTreeWidget* m_tree = nullptr;
};
