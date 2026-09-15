#include "Sidebar.h"
#include "navigation/NavigationManager.h"
#include "tool/ToolRegistry.h"

#include <QVBoxLayout>
#include <QTreeWidget>

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &Sidebar::onItemClicked);
}

void Sidebar::build(NavigationManager* navManager, ToolRegistry* registry)
{
    if (!navManager || !registry) {
        return;
    }

    m_tree->clear();

    auto tree = navManager->buildTree();
    for (const auto& node : tree) {
        auto* groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, node.group.name);
        groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);

        for (const auto& item : node.items) {
            if (!item.visible) {
                continue;
            }

            auto descriptor = registry->findTool(item.toolId);
            if (!descriptor.has_value()) {
                continue;
            }

            auto* toolItem = new QTreeWidgetItem(groupItem);
            toolItem->setText(0, descriptor->name);
            toolItem->setData(0, Qt::UserRole, item.toolId);
        }

        groupItem->setExpanded(true);
    }
}

void Sidebar::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    if (!item) {
        return;
    }

    QString toolId = item->data(0, Qt::UserRole).toString();
    if (!toolId.isEmpty()) {
        emit toolSelected(toolId);
    }
}
