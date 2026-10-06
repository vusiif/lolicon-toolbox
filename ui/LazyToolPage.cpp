#include "LazyToolPage.h"
#include "tool/ToolPage.h"
#include <QVBoxLayout>
LazyToolPage::LazyToolPage(const ToolDescriptor& tool, QWidget* parent) : QWidget(parent), m_tool(tool)
{
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(0, 0, 0, 0);
}
void LazyToolPage::showEvent(QShowEvent* event)
{
    if (!m_page) {
        m_page = m_tool.factory(this);
        if (m_page) layout()->addWidget(m_page);
    }
    QWidget::showEvent(event);
    if (m_page) m_page->onActivated();
}
void LazyToolPage::hideEvent(QHideEvent* event)
{
    if (m_page) m_page->onDeactivated();
    QWidget::hideEvent(event);
}
