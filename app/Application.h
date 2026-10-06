#pragma once
#include <QApplication>
#include <memory>
class AppSettings;
class MainWindow;
class ToolRegistry;
class Application : public QApplication
{
    Q_OBJECT
public:
    Application(int& argc, char** argv);
    ~Application() override;
private:
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<ToolRegistry> m_tools;
    std::unique_ptr<MainWindow> m_window;
};
