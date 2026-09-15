#pragma once

#include <QString>

struct NavigationItem
{
    QString toolId;
    QString groupId;
    int order = 0;
    bool visible = true;
};
