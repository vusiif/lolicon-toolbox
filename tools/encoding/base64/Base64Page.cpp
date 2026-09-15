#include "Base64Page.h"
#include "Base64Codec.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

Base64Page::Base64Page(QWidget* parent)
    : ToolPage(parent)
{
    setupUi();
}

void Base64Page::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setPlaceholderText("Input...");

    auto* buttonLayout = new QHBoxLayout();
    auto* encodeBtn = new QPushButton("Encode", this);
    auto* decodeBtn = new QPushButton("Decode", this);
    auto* clearBtn = new QPushButton("Clear", this);
    buttonLayout->addWidget(encodeBtn);
    buttonLayout->addWidget(decodeBtn);
    buttonLayout->addWidget(clearBtn);
    buttonLayout->addStretch();

    m_outputEdit = new QTextEdit(this);
    m_outputEdit->setPlaceholderText("Output...");
    m_outputEdit->setReadOnly(true);

    auto* copyBtn = new QPushButton("Copy", this);
    auto* outputLayout = new QHBoxLayout();
    outputLayout->addWidget(m_outputEdit, 1);
    outputLayout->addWidget(copyBtn, 0, Qt::AlignTop);

    m_statusLabel = new QLabel(this);

    mainLayout->addWidget(new QLabel("Input", this));
    mainLayout->addWidget(m_inputEdit);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(new QLabel("Output", this));
    mainLayout->addLayout(outputLayout);
    mainLayout->addWidget(m_statusLabel);

    connect(encodeBtn, &QPushButton::clicked, this, [this]() {
        QString input = m_inputEdit->toPlainText();
        QString result = Base64Codec::encode(input);
        m_outputEdit->setPlainText(result);
        m_statusLabel->setText("Encoded successfully");
    });

    connect(decodeBtn, &QPushButton::clicked, this, [this]() {
        QString input = m_inputEdit->toPlainText();
        auto result = Base64Codec::decode(input);
        if (result.has_value()) {
            m_outputEdit->setPlainText(result.value());
            m_statusLabel->setText("Decoded successfully");
        } else {
            m_outputEdit->clear();
            m_statusLabel->setText("Invalid Base64 input");
        }
    });

    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_inputEdit->clear();
        m_outputEdit->clear();
        m_statusLabel->clear();
    });

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_outputEdit->toPlainText();
        if (!text.isEmpty()) {
            m_outputEdit->selectAll();
            m_outputEdit->copy();
            m_statusLabel->setText("Copied to clipboard");
        }
    });
}
