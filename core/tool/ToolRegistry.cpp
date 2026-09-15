#include "ToolRegistry.h"

bool ToolRegistry::registerTool(const ToolDescriptor& descriptor)
{
    if (descriptor.id.isEmpty()) {
        return false;
    }

    if (m_tools.contains(descriptor.id)) {
        return false;
    }

    m_tools.insert(descriptor.id, descriptor);
    return true;
}

std::optional<ToolDescriptor> ToolRegistry::findTool(const QString& id) const
{
    auto it = m_tools.find(id);
    if (it == m_tools.end()) {
        return std::nullopt;
    }
    return *it;
}

QList<ToolDescriptor> ToolRegistry::allTools() const
{
    return m_tools.values();
}

bool ToolRegistry::contains(const QString& id) const
{
    return m_tools.contains(id);
}
