#pragma once

#include <QString>
#include <ElaWidgetToolsDef.h>
#include <functional>

class QWidget;
class ToolPage;

struct ToolDescriptor
{
    QString id;
    QString name;
    ElaIconType::IconName icon = ElaIconType::None;
    QString description;
    std::function<ToolPage*(QWidget* parent)> factory;
};
