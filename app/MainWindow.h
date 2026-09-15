#pragma once

#include <QMainWindow>

class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private:
    void setupUi();

    QStackedWidget* m_pageStack = nullptr;
};
