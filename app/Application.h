#pragma once

#include <QApplication>

class ToolRegistry;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    ToolRegistry& toolRegistry();

private:
    std::unique_ptr<ToolRegistry> m_registry;
};
