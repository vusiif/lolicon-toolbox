#include "ElaNavigationStyle.h"

#include "ElaApplication.h"
#include "ElaNavigationModel.h"
#include "ElaNavigationNode.h"
#include "ElaNavigationView.h"
#include "ElaTheme.h"
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QStyleOption>
ElaNavigationStyle::ElaNavigationStyle(QStyle* style)
{
    _pOpacity = 1;
    _pRotate = 0;
    _pItemHeight = 38;
    _pLastSelectMarkTop = 10.5;
    _pLastSelectMarkBottom = 10.5;
    _pSelectMarkTop = 10.5;
    _pSelectMarkBottom = 10.5;

    // Mark向上
    _lastSelectMarkTopAnimation = new QPropertyAnimation(this, "pLastSelectMarkTop", this);
    connect(_lastSelectMarkTopAnimation, &QPropertyAnimation::valueChanged, this, [=](const QVariant& value) {
        _pNavigationView->viewport()->update();
    });
    _lastSelectMarkTopAnimation->setDuration(80);
    _lastSelectMarkTopAnimation->setEasingCurve(QEasingCurve::InOutSine);

    _selectMarkBottomAnimation = new QPropertyAnimation(this, "pSelectMarkBottom", this);
    connect(_selectMarkBottomAnimation, &QPropertyAnimation::valueChanged, this, [=](const QVariant& value) {
        _pNavigationView->viewport()->update();
    });
    _selectMarkBottomAnimation->setDuration(80);
    _selectMarkBottomAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(_lastSelectMarkTopAnimation, &QPropertyAnimation::finished, this, [=]() {
        _isSelectMarkDisplay = true;
        _lastSelectedNode = nullptr;
        _selectMarkBottomAnimation->setStartValue(0);
        _selectMarkBottomAnimation->setEndValue(10.5);
        _selectMarkBottomAnimation->start();
    });

    // Mark向下
    _lastSelectMarkBottomAnimation = new QPropertyAnimation(this, "pLastSelectMarkBottom", this);
    connect(_lastSelectMarkBottomAnimation, &QPropertyAnimation::valueChanged, this, [=](const QVariant& value) {
        _pNavigationView->viewport()->update();
    });
    _lastSelectMarkBottomAnimation->setDuration(80);
    _lastSelectMarkBottomAnimation->setEasingCurve(QEasingCurve::InOutSine);

    _selectMarkTopAnimation = new QPropertyAnimation(this, "pSelectMarkTop", this);
    connect(_selectMarkTopAnimation, &QPropertyAnimation::valueChanged, this, [=](const QVariant& value) {
        _pNavigationView->viewport()->update();
    });
    _selectMarkTopAnimation->setDuration(80);
    _selectMarkTopAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(_lastSelectMarkBottomAnimation, &QPropertyAnimation::finished, this, [=]() {
        _isSelectMarkDisplay = true;
        _lastSelectedNode = nullptr;
        _selectMarkTopAnimation->setStartValue(0.0);
        _selectMarkTopAnimation->setEndValue(10.5);
        _selectMarkTopAnimation->start();
    });
    _nodeOpacityAnimation = new QPropertyAnimation(this, "pOpacity", this);
    _nodeOpacityAnimation->setDuration(140);
    _nodeOpacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(_nodeOpacityAnimation, &QPropertyAnimation::valueChanged, this, [this]() {
        _pNavigationView->viewport()->update();
    });
    connect(_nodeOpacityAnimation, &QPropertyAnimation::finished, this, [this]() {
        _opacityAnimationTargetNode = nullptr;
    });
    _rotateAnimation = new QPropertyAnimation(this, "pRotate", this);
    _rotateAnimation->setDuration(140);
    _rotateAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(_rotateAnimation, &QPropertyAnimation::valueChanged, this, [this]() {
        _pNavigationView->viewport()->update();
    });
    connect(_rotateAnimation, &QPropertyAnimation::finished, this, [this]() {
        _expandAnimationTargetNode = nullptr;
    });
    _themeMode = eTheme->getThemeMode();
    connect(eTheme, &ElaTheme::themeModeChanged, this, [=](ElaThemeType::ThemeMode themeMode) {
        _themeMode = themeMode;
    });
}

ElaNavigationStyle::~ElaNavigationStyle()
{
}

void ElaNavigationStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
    case QStyle::PE_PanelItemViewItem:
    {
        // Item背景
        if (const QStyleOptionViewItem* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            QModelIndex index = vopt->index;
            ElaNavigationNode* node = static_cast<ElaNavigationNode*>(index.internalPointer());
            if (node->getIsCategoryNode())
            {
                return;
            }
            painter->save();
            if (this->_opacityAnimationTargetNode && node->getParentNode() == this->_opacityAnimationTargetNode)
            {
                painter->setOpacity(_pOpacity);
            }
            painter->setRenderHint(QPainter::Antialiasing);
            QRect itemRect = vopt->rect;
            itemRect.setTop(itemRect.top() + 2);
            itemRect.setBottom(itemRect.bottom() - 2);
            QPainterPath path;
            path.addRoundedRect(itemRect, 5, 5);
            if (vopt->state & QStyle::State_Selected)
            {
                if (index == _pPressIndex)
                {
                    // 选中时点击
                    painter->fillPath(path, ElaThemeColor(_themeMode, BasicHoverAlpha));
                }
                else
                {
                    if (vopt->state & QStyle::State_MouseOver)
                    {
                        // 选中时覆盖
                        painter->fillPath(path, ElaThemeColor(_themeMode, BasicSelectedHoverAlpha));
                    }
                    else
                    {
                        // 选中
                        painter->fillPath(path, ElaThemeColor(_themeMode, BasicSelectedAlpha));
                    }
                }
            }
            else
            {
                if (index == _pPressIndex)
                {
                    // 点击时颜色
                    painter->fillPath(path, ElaThemeColor(_themeMode, BasicPressAlpha));
                }
                else
                {
                    if (vopt->state & QStyle::State_MouseOver)
                    {
                        // 覆盖时颜色
                        painter->fillPath(path, ElaThemeColor(_themeMode, BasicHoverAlpha));
                    }
                }
            }
            painter->restore();
        }
        return;
    }
    case QStyle::PE_PanelItemViewRow:
    {
        // 行背景
        return;
    }
    case QStyle::PE_IndicatorBranch:
    {
        // Quiet hierarchy guides in the indentation area of child rows.
        if (option->state & (QStyle::State_Item | QStyle::State_Sibling))
        {
            painter->save();
            auto color = ElaThemeColor(_themeMode, BasicTextCategory);
            color.setAlpha(90);
            painter->setPen(QPen(color, 1));
            const auto rect = option->rect;
            const int x = rect.center().x();
            const int y = rect.center().y();
            painter->drawLine(x, rect.top(), x, option->state & QStyle::State_Sibling ? rect.bottom() : y);
            if (option->state & QStyle::State_Item)
                painter->drawLine(x, y, rect.right() - 4, y);
            painter->restore();
        }
        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void ElaNavigationStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    int fontPixelSize = eApp->getFontPixelSize();
    switch (element)
    {
    case QStyle::CE_ShapedFrame:
    {
        // viewport视口外的其他区域背景
        return;
    }
    case QStyle::CE_ItemViewItem:
    {
        if (const QStyleOptionViewItem* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            // 背景绘制
            this->drawPrimitive(QStyle::PE_PanelItemViewItem, option, painter, widget);

            // 内容绘制
            QRect itemRect = option->rect;
            painter->save();
            painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            ElaNavigationNode* node = static_cast<ElaNavigationNode*>(vopt->index.internalPointer());
            ElaNavigationModel* model = dynamic_cast<ElaNavigationModel*>(const_cast<QAbstractItemModel*>(vopt->index.model()));

            if (this->_opacityAnimationTargetNode && node->getParentNode() == this->_opacityAnimationTargetNode)
            {
                painter->setOpacity(_pOpacity);
            }

            // 选中特效
            if (_isSelectMarkDisplay && (node == model->getSelectedNode() || node == model->getSelectedExpandedNode()))
            {
                painter->setPen(Qt::NoPen);
                painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                painter->drawRoundedRect(QRectF(itemRect.x() + 3, itemRect.y() + _pSelectMarkTop, 3, itemRect.height() - _pSelectMarkTop - _pSelectMarkBottom), 1.5, 1.5);
            }
            if (node == _lastSelectedNode)
            {
                painter->setPen(Qt::NoPen);
                painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                painter->drawRoundedRect(QRectF(itemRect.x() + 3, itemRect.y() + _pLastSelectMarkTop, 3, itemRect.height() - _pLastSelectMarkTop - _pLastSelectMarkBottom), 1.5, 1.5);
            }

            // 图标绘制
            painter->setPen(vopt->index == _pPressIndex ? ElaThemeColor(_themeMode, BasicTextPress) : ElaThemeColor(_themeMode, BasicText));
            if (node->getAwesome() != ElaIconType::None)
            {
                painter->save();
                QFont iconFont = QFont("ElaAwesome");
                iconFont.setPixelSize(fontPixelSize + 4);
                painter->setFont(iconFont);
                painter->drawText(QRect(itemRect.x(), itemRect.y(), _iconAreaWidth, itemRect.height()), Qt::AlignCenter, QChar(static_cast<ushort>(node->getAwesome())));
                painter->restore();
            }

            int viewWidth = widget->width();
            // 文字绘制
            if (node->getIsCategoryNode())
            {
                QFont categoryFont = painter->font();
                categoryFont.setBold(true);
                painter->setFont(categoryFont);
                painter->setPen(ElaThemeColor(_themeMode, BasicTextCategory));
            }
            else
            {
                painter->setPen((vopt->index == _pPressIndex) ? ElaThemeColor(_themeMode, BasicTextPress) : ElaThemeColor(_themeMode, BasicText));
                if (node->getIsExpanderNode())
                {
                    QFont groupFont = painter->font();
                    groupFont.setBold(true);
                    painter->setFont(groupFont);
                }
            }
            QRect textRect;
            if (node->getAwesome() != ElaIconType::None)
            {
                textRect = QRect(itemRect.x() + _iconAreaWidth, itemRect.y(), itemRect.width() - _textRightSpacing - _indicatorIconAreaWidth - _iconAreaWidth, itemRect.height());
            }
            else
            {
                textRect = QRect(itemRect.x() + _leftPadding, itemRect.y(), itemRect.width() - _textRightSpacing - _indicatorIconAreaWidth - _leftPadding, itemRect.height());
            }
            QString text = painter->fontMetrics().elidedText(node->getNodeTitle(), Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

            if (viewWidth > _pNavigationView->getNavigationBarPrivate()->_pNavigationBarWidth - _indicatorIconAreaWidth)
            {
                // 展开图标 KeyPoints
                if (node->getIsExpanderNode())
                {
                    if (node->getIsHasChild())
                    {
                        QRectF expandIconRect(itemRect.right() - _indicatorIconAreaWidth, itemRect.y(), 17, itemRect.height());
                        painter->save();
                        QFont iconFont = QFont("ElaAwesome");
                        iconFont.setPixelSize(fontPixelSize + 4);
                        painter->setFont(iconFont);
                        painter->translate(expandIconRect.x() + (qreal)expandIconRect.width() / 2, expandIconRect.y() + (qreal)expandIconRect.height() / 2);
                        if (node == _expandAnimationTargetNode)
                        {
                            painter->rotate(_pRotate);
                        }
                        else
                        {
                            if (node->getIsExpanded())
                            {
                                //展开
                                painter->rotate(-180);
                            }
                            else
                            {
                                // 未展开
                                painter->rotate(0);
                            }
                        }
                        painter->translate(-expandIconRect.x() - (qreal)expandIconRect.width() / 2 + 1, -expandIconRect.y() - (qreal)expandIconRect.height() / 2);
                        painter->drawText(expandIconRect, Qt::AlignVCenter, QChar(static_cast<ushort>(ElaIconType::AngleDown)));
                        painter->restore();
                    }
                    if (node->getIsChildHasKeyPoints())
                    {
                        painter->save();
                        painter->setPen(Qt::NoPen);
                        painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                        painter->drawEllipse(QPoint(itemRect.right() - 17, itemRect.y() + 12), 3, 3);
                        painter->restore();
                    }
                }
                else
                {
                    int keyPoints = node->getKeyPoints();
                    if (keyPoints)
                    {
                        // KeyPoints
                        painter->save();
                        painter->setPen(Qt::NoPen);
                        painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                        int keyPointRadius = 8;
                        painter->drawEllipse(QPoint(itemRect.right() - 26, itemRect.y() + itemRect.height() / 2), keyPointRadius, keyPointRadius);
                        painter->setPen(QPen(ElaThemeColor(_themeMode, BasicTextInvert), 2));
                        QFont font = painter->font();
                        font.setBold(true);
                        if (keyPoints > 99)
                        {
                            keyPoints = 99;
                        }
                        if (keyPoints > 9)
                        {
                            font.setPixelSize(fontPixelSize - 3);
                        }
                        else
                        {
                            font.setPixelSize(fontPixelSize - 2);
                        }
                        painter->setFont(font);
                        painter->drawText(QRect(QPoint(itemRect.right() - 26 - keyPointRadius, itemRect.y() + itemRect.height() / 2 - keyPointRadius), QSize(2 * keyPointRadius, 2 * keyPointRadius)), Qt::AlignCenter, QString::number(keyPoints));
                        painter->restore();
                    }
                }
            }
            painter->restore();
        }

        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

QSize ElaNavigationStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const
{
    switch (type)
    {
    case QStyle::CT_ItemViewItem:
    {
        QSize itemSize = QProxyStyle::sizeFromContents(type, option, size, widget);
        itemSize.setHeight(_pItemHeight);
        return itemSize;
    }
    default:
    {
        break;
    }
    }
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

void ElaNavigationStyle::navigationNodeStateChange(const QVariantMap& data)
{
    if (data.contains("Expand") || data.contains("Collapse"))
    {
        const bool expanding = data.contains("Expand");
        ElaNavigationNode* lastExpandNode = _expandAnimationTargetNode;
        _nodeOpacityAnimation->stop();
        _rotateAnimation->stop();
        _expandAnimationTargetNode = data.value(expanding ? "Expand" : "Collapse").value<ElaNavigationNode*>();
        _opacityAnimationTargetNode = expanding ? _expandAnimationTargetNode : nullptr;
        _pOpacity = expanding ? 0.65 : 1.0;
        if (expanding)
        {
            // Visible on the first frame; no transparent first half or queued fades.
            _nodeOpacityAnimation->setStartValue(_pOpacity);
            _nodeOpacityAnimation->setEndValue(1.0);
            _nodeOpacityAnimation->start();
        }
        _rotateAnimation->setStartValue(lastExpandNode == _expandAnimationTargetNode ? _pRotate : (expanding ? 0.0 : -180.0));
        _rotateAnimation->setEndValue(expanding ? -180.0 : 0.0);
        _rotateAnimation->start();
        _pNavigationView->viewport()->update();
    }
    else if (data.contains("SelectMarkChanged"))
    {
        _lastSelectedNode = data.value("LastSelectedNode").value<ElaNavigationNode*>();
        ElaNavigationNode* selectedNode = data.value("SelectedNode").value<ElaNavigationNode*>();
        _lastSelectMarkTopAnimation->stop();
        _lastSelectMarkBottomAnimation->stop();
        _selectMarkTopAnimation->stop();
        _selectMarkBottomAnimation->stop();
        // Folding the selected page's parent changes only its visible marker.
        // Apply that immediately rather than hiding it through two animations.
        if ((selectedNode && selectedNode->getIsExpanderNode()) ||
            (_lastSelectedNode && _lastSelectedNode->getIsExpanderNode()))
        {
            _lastSelectedNode = nullptr;
            _pSelectMarkTop = _pSelectMarkBottom = 10.5;
            _isSelectMarkDisplay = true;
            _pNavigationView->viewport()->update();
            return;
        }
        bool direction = _compareItemY(selectedNode, _lastSelectedNode);
        _pLastSelectMarkTop = 10.5;
        _pLastSelectMarkBottom = 10.5;
        _pSelectMarkTop = 10.5;
        _pSelectMarkBottom = 10.5;
        if (direction)
        {
            _lastSelectMarkTopAnimation->setStartValue(10.5);
            _lastSelectMarkTopAnimation->setEndValue(0.0);
            _lastSelectMarkTopAnimation->start();
            _lastSelectMarkBottomAnimation->stop();
            _selectMarkTopAnimation->stop();
            _isSelectMarkDisplay = false;
        }
        else
        {
            _lastSelectMarkBottomAnimation->setStartValue(10.5);
            _lastSelectMarkBottomAnimation->setEndValue(0.0);
            _lastSelectMarkBottomAnimation->start();
            _lastSelectMarkTopAnimation->stop();
            _selectMarkBottomAnimation->stop();
            _isSelectMarkDisplay = false;
        }
    }
}

bool ElaNavigationStyle::_compareItemY(ElaNavigationNode* node1, ElaNavigationNode* node2)
{
    // 返回true 即node1 高于 node2
    if (!node1)
    {
        return false;
    }
    if (!node2)
    {
        return true;
    }
    // 同一父节点
    if (node1->getParentNode() == node2->getParentNode())
    {
        if (node1->getModelIndex().row() < node2->getModelIndex().row())
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        ElaNavigationNode* node1OriginalNode = node1->getOriginalNode();
        ElaNavigationNode* node2OriginalNode = node2->getOriginalNode();
        // 不同父节点  相同起源节点
        if (node1OriginalNode == node2OriginalNode)
        {
            int node1Depth = node1->getDepth();
            int node2Depth = node2->getDepth();
            // 相同深度
            if (node1Depth == node2Depth)
            {
                ElaNavigationNode* node1ParentNode = node1->getParentNode();
                ElaNavigationNode* node2ParentNode = node2->getParentNode();
                if (node1ParentNode->getModelIndex().row() < node2ParentNode->getModelIndex().row())
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                if (node1Depth < node2Depth)
                {
                    ElaNavigationNode* node2ParentNode = node2->getParentNode();
                    while (node2ParentNode->getDepth() != node1Depth)
                    {
                        node2ParentNode = node2ParentNode->getParentNode();
                    }
                    // 父子节点关系
                    if (node1 == node2ParentNode)
                    {
                        return true;
                    }
                    if (node1->getModelIndex().row() < node2ParentNode->getModelIndex().row())
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    ElaNavigationNode* node1ParentNode = node1->getParentNode();
                    while (node1ParentNode->getDepth() != node2Depth)
                    {
                        node1ParentNode = node1ParentNode->getParentNode();
                    }
                    if (node2 == node1ParentNode)
                    {
                        return false;
                    }
                    if (node1ParentNode->getModelIndex().row() < node2->getModelIndex().row())
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
        }
        else
        {
            if (node1OriginalNode->getModelIndex().row() < node2OriginalNode->getModelIndex().row())
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
