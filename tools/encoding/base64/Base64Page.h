#pragma once

#include "tool/ToolPage.h"

class QTextEdit;
class QLabel;

class Base64Page : public ToolPage
{
    Q_OBJECT

public:
    explicit Base64Page(QWidget* parent = nullptr);
    ~Base64Page() override = default;

private:
    void setupUi();

    QTextEdit* m_inputEdit = nullptr;
    QTextEdit* m_outputEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
};
