#include "NavigationManager.h"
#include "tool/ToolRegistry.h"
#include <algorithm>

bool NavigationManager::addGroup(const NavigationGroup& group)
{
    if (group.id.trimmed().isEmpty() || group.name.trimmed().isEmpty()) return false;
    for (const auto& existing : m_groups)
        if (existing.id == group.id) return false;
    m_groups.append(group);
    return true;
}

bool NavigationManager::addItem(const NavigationItem& item)
{
    if (item.toolId.trimmed().isEmpty()) return false;
    for (const auto& existing : m_items)
        if (existing.toolId == item.toolId) return false;
    for (const auto& group : m_groups) {
        if (group.id == item.groupId) {
            m_items.append(item);
            return true;
        }
    }
    return false;
}

QList<NavigationSection> NavigationManager::sections(const ToolRegistry& tools) const
{
    auto groups = m_groups;
    std::ranges::sort(groups, [](const auto& a, const auto& b) {
        return a.order == b.order ? a.id < b.id : a.order < b.order;
    });
    QList<NavigationSection> result;
    for (const auto& group : groups) {
        NavigationSection section{.group = group, .items = {}};
        for (const auto& item : m_items)
            if (item.groupId == group.id && item.visible && tools.contains(item.toolId))
                section.items.append(item);
        std::ranges::sort(section.items, [](const auto& a, const auto& b) {
            return a.order == b.order ? a.toolId < b.toolId : a.order < b.order;
        });
        // Don't display categories that have no available, visible tools.
        if (!section.items.isEmpty()) result.append(section);
    }
    return result;
}
