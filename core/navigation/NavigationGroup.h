#pragma once
#include <QString>

struct NavigationGroup
{
    QString id;
    QString name; // Translation source in the Navigation context.
    int order = 0;
};
