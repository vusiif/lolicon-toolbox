#include "Base64Page.h"
#include "Base64Codec.h"
#include "theme/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFont>

Base64Page::Base64Page(QWidget* parent)
    : ToolPage(parent)
{
    setupUi();
}

QPushButton* Base64Page::createButton(const QString& text, bool primary)
{
    auto* btn = new QPushButton(text, this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(36);
    btn->setMinimumWidth(80);

    QFont btnFont = btn->font();
    btnFont.setPointSize(11);
    btn->setFont(btnFont);

    if (primary) {
        btn->setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background-color: %1;
                color: %2;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-weight: 600;
            }
            QPushButton:hover {
                background-color: %3;
            }
            QPushButton:pressed {
                background-color: %4;
            }
        )")
        .arg(theme::Accent)
        .arg(theme::WindowBg)
        .arg(theme::AccentHover)
        .arg(theme::AccentPressed));
    } else {
        btn->setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background-color: %1;
                color: %2;
                border: 1px solid %3;
                border-radius: 6px;
                padding: 0 20px;
            }
            QPushButton:hover {
                background-color: %4;
                border-color: %5;
            }
            QPushButton:pressed {
                background-color: %6;
            }
        )")
        .arg(theme::ButtonBg)
        .arg(theme::TextPrimary)
        .arg(theme::Border)
        .arg(theme::ButtonHover)
        .arg(theme::Surface3)
        .arg(theme::ButtonPressed));
    }
    return btn;
}

void Base64Page::setupUi()
{
    QString textEditStyle = QStringLiteral(R"(
        QTextEdit {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 8px;
            padding: 12px;
            font-family: "Cascadia Code", "Consolas", "Courier New", monospace;
            font-size: 13px;
            selection-background-color: %4;
        }
        QTextEdit:focus {
            border-color: %5;
        }
    )")
    .arg(theme::InputBg)
    .arg(theme::TextPrimary)
    .arg(theme::InputBorder)
    .arg(theme::Accent)
    .arg(theme::InputFocus);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 28, 32, 28);
    mainLayout->setSpacing(20);

    auto* titleLabel = new QLabel("Base64 Encoder / Decoder", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme::TextPrimary));

    auto* descLabel = new QLabel("Encode text to Base64 or decode Base64 to text.", this);
    QFont descFont = descLabel->font();
    descFont.setPointSize(11);
    descLabel->setFont(descFont);
    descLabel->setStyleSheet(QStringLiteral("color: %1; margin-bottom: 8px;").arg(theme::TextSecondary));

    auto* inputHeader = new QLabel("Input", this);
    QFont sectionFont = inputHeader->font();
    sectionFont.setPointSize(11);
    sectionFont.setWeight(QFont::DemiBold);
    inputHeader->setFont(sectionFont);
    inputHeader->setStyleSheet(QStringLiteral("color: %1;").arg(theme::TextSecondary));

    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setPlaceholderText("Enter text to encode, or Base64 to decode...");
    m_inputEdit->setMinimumHeight(120);
    m_inputEdit->setStyleSheet(textEditStyle);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    auto* encodeBtn = createButton("Encode", true);
    auto* decodeBtn = createButton("Decode", true);
    auto* clearBtn = createButton("Clear");
    buttonLayout->addWidget(encodeBtn);
    buttonLayout->addWidget(decodeBtn);
    buttonLayout->addWidget(clearBtn);
    buttonLayout->addStretch();

    auto* outputHeader = new QLabel("Output", this);
    outputHeader->setFont(sectionFont);
    outputHeader->setStyleSheet(QStringLiteral("color: %1;").arg(theme::TextSecondary));

    m_outputEdit = new QTextEdit(this);
    m_outputEdit->setPlaceholderText("Result will appear here...");
    m_outputEdit->setReadOnly(true);
    m_outputEdit->setMinimumHeight(120);
    m_outputEdit->setStyleSheet(textEditStyle);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::TextSecondary));
    auto* copyBtn = createButton("Copy Result");
    bottomLayout->addWidget(m_statusLabel, 1);
    bottomLayout->addWidget(copyBtn);

    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(descLabel);
    mainLayout->addWidget(inputHeader);
    mainLayout->addWidget(m_inputEdit, 1);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(outputHeader);
    mainLayout->addWidget(m_outputEdit, 1);
    mainLayout->addLayout(bottomLayout);

    connect(encodeBtn, &QPushButton::clicked, this, [this]() {
        QString input = m_inputEdit->toPlainText();
        if (input.isEmpty()) {
            m_statusLabel->setText("Input is empty");
            m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::Danger));
            return;
        }
        std::string result = Base64Codec::encode(input.toStdString());
        m_outputEdit->setPlainText(QString::fromStdString(result));
        m_statusLabel->setText("Encoded successfully");
        m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::Success));
    });

    connect(decodeBtn, &QPushButton::clicked, this, [this]() {
        QString input = m_inputEdit->toPlainText();
        if (input.isEmpty()) {
            m_statusLabel->setText("Input is empty");
            m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::Danger));
            return;
        }
        auto result = Base64Codec::decode(input.toStdString());
        if (result.has_value()) {
            m_outputEdit->setPlainText(QString::fromStdString(result.value()));
            m_statusLabel->setText("Decoded successfully");
            m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::Success));
        } else {
            m_outputEdit->clear();
            m_statusLabel->setText("Invalid Base64 input");
            m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::Danger));
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
            m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme::Success));
        }
    });
}