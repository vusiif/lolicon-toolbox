#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class NavigationManager;
class ToolRegistry;

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void build(NavigationManager* navManager, ToolRegistry* registry);

    QWidget* bottomArea() const;

signals:
    void toolSelected(const QString& toolId);

private:
    void onItemClicked(QTreeWidgetItem* item, int column);

    QVBoxLayout* m_layout = nullptr;
    QTreeWidget* m_tree = nullptr;
    QWidget* m_bottomArea = nullptr;
};