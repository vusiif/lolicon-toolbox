#pragma once

#include <QWidget>

class ToolPage : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPage(QWidget* parent = nullptr);
    ~ToolPage() override = default;

    virtual void onActivated();
    virtual void onDeactivated();
};
