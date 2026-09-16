#include "Sidebar.h"
#include "Theme.h"
#include "navigation/NavigationManager.h"
#include "tool/ToolRegistry.h"

#include <QVBoxLayout>
#include <QTreeWidget>
#include <QLabel>

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(0);
    m_tree->setAnimated(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_tree->setStyleSheet(QStringLiteral(R"(
        QTreeWidget {
            background-color: %1;
            color: %2;
            border: none;
            outline: none;
            font-size: 13px;
            padding: 4px 0;
        }
        QTreeWidget::item {
            padding: 8px 16px;
            border: none;
            border-radius: 6px;
            margin: 1px 6px;
        }
        QTreeWidget::item:hover {
            background-color: %3;
        }
        QTreeWidget::item:selected {
            background-color: %4;
            color: %5;
        }
        QTreeWidget::branch {
            background-color: %1;
            border: none;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 4px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: %6;
            border-radius: 2px;
            min-height: 20px;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
        }
    )")
    .arg(theme::SidebarBg)
    .arg(theme::TextPrimary)
    .arg(theme::SidebarHover)
    .arg(theme::SidebarActive)
    .arg(theme::Accent)
    .arg(theme::ScrollbarThumb));

    m_bottomArea = new QWidget(this);
    m_bottomArea->setFixedHeight(48);
    m_bottomArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::SidebarBg));

    m_layout->addWidget(m_tree, 1);
    m_layout->addWidget(m_bottomArea, 0);

    connect(m_tree, &QTreeWidget::itemClicked, this, &Sidebar::onItemClicked);
}

void Sidebar::build(NavigationManager* navManager, ToolRegistry* registry)
{
    if (!navManager || !registry) {
        return;
    }

    m_tree->clear();

    auto tree = navManager->buildTree();
    for (const auto& node : tree) {
        auto* groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, node.group.name.toUpper());
        groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);
        groupItem->setData(0, Qt::UserRole, QString());

        QFont groupFont = groupItem->font(0);
        groupFont.setPointSize(9);
        groupFont.setWeight(QFont::DemiBold);
        groupItem->setFont(0, groupFont);
        groupItem->setForeground(0, QColor(theme::TextSecondary));

        for (const auto& item : node.items) {
            if (!item.visible) {
                continue;
            }

            auto descriptor = registry->findTool(item.toolId);
            if (!descriptor.has_value()) {
                continue;
            }

            auto* toolItem = new QTreeWidgetItem(groupItem);
            toolItem->setText(0, descriptor->name);
            toolItem->setData(0, Qt::UserRole, item.toolId);

            QFont toolFont = toolItem->font(0);
            toolFont.setPointSize(12);
            toolItem->setFont(0, toolFont);
        }

        groupItem->setExpanded(true);
    }
}

QWidget* Sidebar::bottomArea() const
{
    return m_bottomArea;
}

void Sidebar::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    if (!item) {
        return;
    }

    QString toolId = item->data(0, Qt::UserRole).toString();
    if (!toolId.isEmpty()) {
        emit toolSelected(toolId);
    }
}