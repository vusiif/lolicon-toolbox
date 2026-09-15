#include "ToolPageManager.h"
#include "ToolRegistry.h"
#include "ToolPage.h"

#include <QStackedWidget>

ToolPageManager::ToolPageManager(QStackedWidget* stack, ToolRegistry& registry, QObject* parent)
    : QObject(parent)
    , m_stack(stack)
    , m_registry(registry)
{
}

void ToolPageManager::openTool(const QString& toolId)
{
    if (toolId == m_currentToolId) {
        return;
    }

    auto* page = ensurePage(toolId);
    if (!page) {
        return;
    }

    if (!m_currentToolId.isEmpty()) {
        auto it = m_pages.find(m_currentToolId);
        if (it != m_pages.end() && *it) {
            (*it)->onDeactivated();
        }
    }

    m_stack->setCurrentWidget(page);
    m_currentToolId = toolId;
    page->onActivated();
}

QString ToolPageManager::currentToolId() const
{
    return m_currentToolId;
}

ToolPage* ToolPageManager::ensurePage(const QString& toolId)
{
    auto it = m_pages.find(toolId);
    if (it != m_pages.end()) {
        return *it;
    }

    auto descriptor = m_registry.findTool(toolId);
    if (!descriptor.has_value()) {
        return nullptr;
    }

    auto* page = descriptor->factory(m_stack);
    if (!page) {
        return nullptr;
    }

    m_pages.insert(toolId, page);
    m_stack->addWidget(page);
    return page;
}
