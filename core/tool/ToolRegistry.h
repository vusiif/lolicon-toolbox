#pragma once

#include "ToolDescriptor.h"

#include <QMap>
#include <QString>
#include <optional>

class ToolRegistry
{
public:
    ToolRegistry() = default;

    bool registerTool(const ToolDescriptor& descriptor);
    std::optional<ToolDescriptor> findTool(const QString& id) const;
    QList<ToolDescriptor> allTools() const;
    bool contains(const QString& id) const;

private:
    QMap<QString, ToolDescriptor> m_tools;
};
