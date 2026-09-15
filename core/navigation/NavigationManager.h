#pragma once

#include "NavigationGroup.h"
#include "NavigationItem.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

struct NavigationNode
{
    NavigationGroup group;
    QList<NavigationItem> items;
};

class NavigationManager : public QObject
{
    Q_OBJECT

public:
    explicit NavigationManager(QObject* parent = nullptr);

    void registerGroup(const NavigationGroup& group);
    void registerItem(const NavigationItem& item);

    QList<NavigationNode> buildTree() const;
    QList<NavigationGroup> groups() const;
    QList<NavigationItem> itemsForGroup(const QString& groupId) const;

private:
    QMap<QString, NavigationGroup> m_groups;
    QMap<QString, QList<NavigationItem>> m_items;
};
