#include "Sidebar.h"
#include "theme/Theme.h"
#include "navigation/NavigationManager.h"
#include "tool/ToolRegistry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVariantAnimation>
#include <QScrollArea>
#include <QFrame>

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    m_widthAnim = new QVariantAnimation(this);
    m_widthAnim->setDuration(250);
    m_widthAnim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_widthAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
        setFixedWidth(val.toInt());
    });

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    m_headerWidget = new QWidget(this);
    m_headerWidget->setFixedHeight(44);
    m_headerWidget->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::SidebarBg()));
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(10, 4, 10, 4);

    m_toggleBtn = new QPushButton(QStringLiteral("\u2630"), m_headerWidget);
    m_toggleBtn->setFixedSize(36, 36);
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    m_toggleBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            color: %1;
            border: none;
            border-radius: 6px;
            font-size: 18px;
        }
        QPushButton:hover {
            background-color: %2;
        }
        QPushButton:pressed {
            background-color: %3;
        }
    )")
    .arg(theme::TextPrimary())
    .arg(theme::SidebarHover())
    .arg(theme::SidebarActive()));
    connect(m_toggleBtn, &QPushButton::clicked, this, &Sidebar::toggle);

    headerLayout->addWidget(m_toggleBtn);
    headerLayout->addStretch();

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::SidebarBg()));

    m_listWidget = new QWidget();
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(6, 4, 6, 4);
    m_listLayout->setSpacing(2);
    m_listLayout->addStretch();

    scrollArea->setWidget(m_listWidget);

    m_bottomArea = new QWidget(this);
    m_bottomArea->setFixedHeight(48);
    m_bottomArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::SidebarBg()));

    auto* bottomLayout = new QHBoxLayout(m_bottomArea);
    bottomLayout->setContentsMargins(16, 0, 16, 0);

    auto* settingsBtn = new QPushButton(QStringLiteral("\u2699"), m_bottomArea);
    settingsBtn->setFixedSize(36, 36);
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setToolTip(QStringLiteral("Settings"));
    settingsBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            color: %1;
            border: none;
            border-radius: 6px;
            font-size: 16px;
        }
        QPushButton:hover {
            background-color: %2;
        }
        QPushButton:pressed {
            background-color: %3;
        }
    )")
    .arg(theme::TextSecondary())
    .arg(theme::SidebarHover())
    .arg(theme::SidebarActive()));
    connect(settingsBtn, &QPushButton::clicked, this, &Sidebar::settingsClicked);

    m_versionLabel = new QLabel("v0.1.0", m_bottomArea);
    m_versionLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(theme::TextDisabled()));

    bottomLayout->addWidget(settingsBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_versionLabel);

    m_mainLayout->addWidget(m_headerWidget, 0);
    m_mainLayout->addWidget(scrollArea, 1);
    m_mainLayout->addWidget(m_bottomArea, 0);

    setFixedWidth(m_expandedWidth);
}

void Sidebar::build(NavigationManager* navManager, ToolRegistry* registry)
{
    if (!navManager || !registry) {
        return;
    }

    m_items.clear();

    auto tree = navManager->buildTree();
    for (const auto& node : tree) {
        for (const auto& item : node.items) {
            if (!item.visible) continue;
            auto descriptor = registry->findTool(item.toolId);
            if (!descriptor.has_value()) continue;

            SidebarToolItem si;
            si.toolId = item.toolId;
            si.icon = descriptor->icon;
            si.name = descriptor->name;
            m_items.append(si);
        }
    }

    rebuildItems();
}

void Sidebar::rebuildItems()
{
    QLayoutItem* child;
    while ((child = m_listLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }

    for (int i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];

        bool selected = (i == m_selectedIndex);
        QString bgColor = selected ? theme::SidebarActive() : "transparent";
        QString borderColor = selected ? theme::Accent() : "transparent";

        auto* btn = new QPushButton(m_listWidget);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(40);
        btn->setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background-color: %1;
                color: %2;
                border: none;
                border-left: 3px solid %3;
                border-radius: 0 6px 6px 0;
                text-align: left;
                padding: 0 10px;
            }
            QPushButton:hover {
                background-color: %4;
            }
        )")
        .arg(bgColor, theme::TextPrimary(), borderColor, theme::SidebarHover()));

        auto* btnLayout = new QHBoxLayout(btn);
        btnLayout->setContentsMargins(7, 0, 10, 0);
        btnLayout->setSpacing(0);

        auto* iconLabel = new QLabel(item.icon, btn);
        iconLabel->setFixedSize(36, 36);
        iconLabel->setAlignment(Qt::AlignCenter);
        QFont iconFont = iconLabel->font();
        iconFont.setPointSize(13);
        iconFont.setWeight(QFont::Bold);
        iconLabel->setFont(iconFont);
        iconLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(
            selected ? theme::Accent() : theme::TextSecondary()));

        auto* nameLabel = new QLabel(item.name, btn);
        QFont nameFont = nameLabel->font();
        nameFont.setPointSize(12);
        nameLabel->setFont(nameFont);
        nameLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none; margin-left: 8px;").arg(theme::TextPrimary()));
        nameLabel->setVisible(m_expanded);

        btnLayout->addWidget(iconLabel, 0);
        btnLayout->addWidget(nameLabel, 1);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onItemClicked(i);
        });

        m_listLayout->addWidget(btn);
    }

    m_listLayout->addStretch();
}

QWidget* Sidebar::bottomArea() const
{
    return m_bottomArea;
}

void Sidebar::expand()
{
    if (m_expanded) return;
    m_expanded = true;
    m_widthAnim->setStartValue(m_collapsedWidth);
    m_widthAnim->setEndValue(m_expandedWidth);
    m_widthAnim->start();

    for (int i = 0; i < m_listLayout->count(); ++i) {
        QLayoutItem* item = m_listLayout->itemAt(i);
        if (item && item->widget()) {
            QWidget* row = item->widget();
            QList<QLabel*> labels = row->findChildren<QLabel*>();
            if (labels.size() >= 2) {
                labels[1]->setVisible(true);
            }
        }
    }
    m_versionLabel->show();
    emit expandStateChanged(true);
}

void Sidebar::retract()
{
    if (!m_expanded) return;
    m_expanded = false;
    m_widthAnim->setStartValue(m_expandedWidth);
    m_widthAnim->setEndValue(m_collapsedWidth);
    m_widthAnim->start();

    for (int i = 0; i < m_listLayout->count(); ++i) {
        QLayoutItem* item = m_listLayout->itemAt(i);
        if (item && item->widget()) {
            QWidget* row = item->widget();
            QList<QLabel*> labels = row->findChildren<QLabel*>();
            if (labels.size() >= 2) {
                labels[1]->setVisible(false);
            }
        }
    }
    m_versionLabel->hide();
    emit expandStateChanged(false);
}

void Sidebar::toggle()
{
    m_expanded ? retract() : expand();
}

bool Sidebar::isExpanded() const
{
    return m_expanded;
}

void Sidebar::onItemClicked(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    m_selectedIndex = index;
    rebuildItems();
    emit toolSelected(m_items[index].toolId);
}
