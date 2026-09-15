#pragma once

#include <QString>
#include <functional>

class QWidget;
class ToolPage;

struct ToolDescriptor
{
    QString id;
    QString name;
    QString description;
    std::function<ToolPage*(QWidget* parent)> factory;
};
