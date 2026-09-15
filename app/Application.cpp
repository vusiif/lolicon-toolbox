#include "Application.h"
#include "tool/ToolRegistry.h"

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
    , m_registry(std::make_unique<ToolRegistry>())
{
    setApplicationName("Toolbox");
    setApplicationVersion("0.1.0");
}

Application::~Application() = default;

ToolRegistry& Application::toolRegistry()
{
    return *m_registry;
}
