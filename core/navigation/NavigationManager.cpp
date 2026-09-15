#include "NavigationManager.h"

#include <algorithm>

NavigationManager::NavigationManager(QObject* parent)
    : QObject(parent)
{
}

void NavigationManager::registerGroup(const NavigationGroup& group)
{
    m_groups.insert(group.id, group);
}

void NavigationManager::registerItem(const NavigationItem& item)
{
    m_items[item.groupId].append(item);
}

QList<NavigationNode> NavigationManager::buildTree() const
{
    QList<NavigationNode> result;

    auto sortedGroups = groups();
    for (const auto& group : sortedGroups) {
        NavigationNode node;
        node.group = group;
        node.items = itemsForGroup(group.id);

        std::sort(node.items.begin(), node.items.end(),
            [](const NavigationItem& a, const NavigationItem& b) {
                return a.order < b.order;
            });

        result.append(node);
    }

    return result;
}

QList<NavigationGroup> NavigationManager::groups() const
{
    auto result = m_groups.values();
    std::sort(result.begin(), result.end(),
        [](const NavigationGroup& a, const NavigationGroup& b) {
            return a.order < b.order;
        });
    return result;
}

QList<NavigationItem> NavigationManager::itemsForGroup(const QString& groupId) const
{
    auto it = m_items.find(groupId);
    if (it == m_items.end()) {
        return {};
    }

    auto result = *it;
    std::sort(result.begin(), result.end(),
        [](const NavigationItem& a, const NavigationItem& b) {
            return a.order < b.order;
        });
    return result;
}
