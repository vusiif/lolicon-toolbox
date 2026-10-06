#pragma once
#include "tool/ToolPage.h"
class ElaPlainTextEdit;
class ElaPushButton;
class QLabel;
class Base64Page : public ToolPage
{
    Q_OBJECT
public:
    explicit Base64Page(QWidget* parent = nullptr);
protected:
    void changeEvent(QEvent* event) override;
private:
    enum class Status { Ready, Empty, Encoded, Decoded, Invalid, Copied };
    void retranslate();
    void setStatus(Status status);
    ElaPlainTextEdit* m_input;
    ElaPlainTextEdit* m_output;
    ElaPushButton* m_encode;
    ElaPushButton* m_decode;
    ElaPushButton* m_clear;
    ElaPushButton* m_copy;
    QLabel* m_title;
    QLabel* m_description;
    QLabel* m_inputLabel;
    QLabel* m_outputLabel;
    QLabel* m_statusLabel;
    Status m_status = Status::Ready;
};
