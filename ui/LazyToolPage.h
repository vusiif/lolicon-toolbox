#pragma once
#include <QWidget>
#include "tool/ToolDescriptor.h"
class ToolPage;
class LazyToolPage : public QWidget
{
    Q_OBJECT
public:
    explicit LazyToolPage(const ToolDescriptor& tool, QWidget* parent = nullptr);
    ToolPage* page() const { return m_page; }
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    ToolDescriptor m_tool;
    ToolPage* m_page = nullptr;
};
