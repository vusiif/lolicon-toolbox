#include "Base64Page.h"
#include "Base64Codec.h"
#include "UiTheme.h"
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QClipboard>
#include <QGuiApplication>
#include <QEvent>

Base64Page::Base64Page(QWidget* parent) : ToolPage(parent)
{
    setObjectName("base64Page");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    // QScrollArea enables autofill on its content; inherit Ela's live backdrop
    // instead of retaining the palette captured when the page was created.
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(14);
    m_title = ui::label({}, 18, ui::Tone::Primary, this);
    m_description = ui::label({}, 10, ui::Tone::Secondary, this);
    m_inputLabel = ui::label({}, 10, ui::Tone::Secondary, this);
    m_outputLabel = ui::label({}, 10, ui::Tone::Secondary, this);
    m_input = new ElaPlainTextEdit(this);
    m_input->setObjectName("inputText");
    m_input->setMinimumHeight(100);
    m_input->setFont(QFont("Consolas", 11));
    m_output = new ElaPlainTextEdit(this);
    m_output->setObjectName("outputText");
    m_output->setReadOnly(true);
    m_output->setMinimumHeight(100);
    m_output->setFont(m_input->font());
    auto button = [this](const char* name, bool primary) {
        auto* result = new ElaPushButton(this);
        result->setObjectName(name);
        result->setMinimumWidth(100);
        result->setCursor(Qt::PointingHandCursor);
        if (primary) ui::primaryButton(result);
        return result;
    };
    m_encode = button("encodeButton", true);
    m_decode = button("decodeButton", true);
    m_clear = button("clearButton", false);
    m_copy = button("copyButton", false);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(10);
    actions->addWidget(m_encode);
    actions->addWidget(m_decode);
    actions->addWidget(m_clear);
    actions->addStretch();
    auto* bottom = new QHBoxLayout;
    m_statusLabel = ui::label({}, 10, ui::Tone::Secondary, this);
    m_statusLabel->setObjectName("statusLabel");
    bottom->addWidget(m_statusLabel, 1);
    bottom->addWidget(m_copy);
    layout->addWidget(m_title);
    layout->addWidget(m_description);
    layout->addWidget(m_inputLabel);
    layout->addWidget(m_input, 1);
    layout->addLayout(actions);
    layout->addWidget(m_outputLabel);
    layout->addWidget(m_output, 1);
    layout->addLayout(bottom);
    connect(m_encode, &ElaPushButton::clicked, this, [this]() {
        const auto bytes = m_input->toPlainText().toUtf8();
        if (bytes.isEmpty()) { setStatus(Status::Empty); return; }
        const auto result = Base64Codec::encode(std::string(bytes.constData(), bytes.size()));
        m_output->setPlainText(QString::fromLatin1(result.data(), qsizetype(result.size())));
        setStatus(Status::Encoded);
    });
    connect(m_decode, &ElaPushButton::clicked, this, [this]() {
        const auto bytes = m_input->toPlainText().toUtf8();
        if (bytes.isEmpty()) { setStatus(Status::Empty); return; }
        const auto result = Base64Codec::decode(std::string(bytes.constData(), bytes.size()));
        if (!result) { m_output->clear(); setStatus(Status::Invalid); return; }
        m_output->setPlainText(QString::fromUtf8(result->data(), qsizetype(result->size())));
        setStatus(Status::Decoded);
    });
    connect(m_clear, &ElaPushButton::clicked, this, [this]() {
        m_input->clear(); m_output->clear(); setStatus(Status::Ready);
    });
    connect(m_copy, &ElaPushButton::clicked, this, [this]() {
        if (m_output->toPlainText().isEmpty()) return;
        QGuiApplication::clipboard()->setText(m_output->toPlainText());
        setStatus(Status::Copied);
    });
    retranslate();
}
void Base64Page::setStatus(Status status)
{
    m_status = status;
    ui::setTone(m_statusLabel, status == Status::Empty || status == Status::Invalid ? ui::Tone::Danger
        : status == Status::Ready ? ui::Tone::Secondary : ui::Tone::Success);
    switch (status) {
    case Status::Ready: m_statusLabel->clear(); break;
    case Status::Empty: m_statusLabel->setText(tr("Input is empty")); break;
    case Status::Encoded: m_statusLabel->setText(tr("Encoded successfully")); break;
    case Status::Decoded: m_statusLabel->setText(tr("Decoded successfully")); break;
    case Status::Invalid: m_statusLabel->setText(tr("Invalid Base64 input")); break;
    case Status::Copied: m_statusLabel->setText(tr("Copied to clipboard")); break;
    }
}
void Base64Page::retranslate()
{
    m_title->setText(tr("Base64 Encoder / Decoder"));
    m_description->setText(tr("Encode text to Base64 or decode Base64 to text."));
    m_inputLabel->setText(tr("Input"));
    m_outputLabel->setText(tr("Output"));
    m_encode->setText(tr("Encode")); m_decode->setText(tr("Decode"));
    m_clear->setText(tr("Clear")); m_copy->setText(tr("Copy Result"));
    m_input->setPlaceholderText(tr("Enter text to encode, or Base64 to decode..."));
    m_output->setPlaceholderText(tr("Result will appear here..."));
    setStatus(m_status);
}
void Base64Page::changeEvent(QEvent* event)
{
    ToolPage::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
