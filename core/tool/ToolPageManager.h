#pragma once

#include <QMap>
#include <QObject>
#include <QString>

class QStackedWidget;
class ToolRegistry;
class ToolPage;

class ToolPageManager : public QObject
{
    Q_OBJECT

public:
    ToolPageManager(QStackedWidget* stack, ToolRegistry& registry, QObject* parent = nullptr);

    void openTool(const QString& toolId);
    QString currentToolId() const;
    QStackedWidget* stackWidget() const { return m_stack; }

private:
    ToolPage* ensurePage(const QString& toolId);

    QStackedWidget* m_stack;
    ToolRegistry& m_registry;
    QMap<QString, ToolPage*> m_pages;
    QString m_currentToolId;
};
