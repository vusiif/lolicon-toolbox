#pragma once
#include "NavigationGroup.h"
#include "NavigationItem.h"
#include <QList>

class ToolRegistry;

struct NavigationSection
{
    NavigationGroup group;
    QList<NavigationItem> items;
};

// Navigation placement is separate from tool implementation and registration.
class NavigationManager
{
public:
    bool addGroup(const NavigationGroup& group);
    bool addItem(const NavigationItem& item);
    QList<NavigationSection> sections(const ToolRegistry& tools) const;
private:
    QList<NavigationGroup> m_groups;
    QList<NavigationItem> m_items;
};
