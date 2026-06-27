#include "nodeitem.h"

#include "core/fsnode.h"
#include "graphscene.h"

#include <algorithm>

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace ui {

NodeItem::NodeItem(const core::FsNode *node, bool hasChildren, bool collapsed,
                   std::function<void(const core::FsNode *)> onToggle, GraphScene *scene)
    : m_node(node), m_hasChildren(hasChildren), m_collapsed(collapsed),
      m_onToggle(std::move(onToggle)), m_scene(scene) {
    const int rows = std::min<int>(m_node->files.size(), MaxFilesShown);
    const bool moreRow = m_node->fileCount > MaxFilesShown;
    m_height = HeaderH + (rows + (moreRow ? 1 : 0)) * RowH + 8.0;
    if (m_height < HeaderH + RowH)
        m_height = HeaderH + RowH;

    m_toggleRect = QRectF(Width - 24.0, 6.0, 16.0, 16.0);

    setFlag(ItemIsMovable, true); // drag-to-arrange (layout only — ADR-300)
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setCursor(Qt::ArrowCursor);
}

QRectF NodeItem::boundingRect() const {
    return QRectF(0, 0, Width, m_height);
}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    const QPalette pal = qApp->palette();
    const QColor fill = pal.color(QPalette::Base);
    const QColor header = pal.color(QPalette::Highlight);
    const QColor headerText = pal.color(QPalette::HighlightedText);
    const QColor text = pal.color(QPalette::Text);
    QColor border = pal.color(QPalette::Mid);
    if (isSelected())
        border = pal.color(QPalette::Highlight);

    const QRectF r = boundingRect();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Body
    QPainterPath body;
    body.addRoundedRect(r, 8, 8);
    painter->fillPath(body, fill);

    // Header band
    QPainterPath head;
    head.addRoundedRect(QRectF(0, 0, Width, HeaderH), 8, 8);
    head.addRect(QRectF(0, HeaderH - 8, Width, 8)); // square off the bottom corners
    painter->fillPath(head.simplified(), header);

    // Title (elided)
    painter->setPen(headerText);
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    painter->setFont(titleFont);
    const qreal titleRight = m_hasChildren ? m_toggleRect.left() - 6.0 : Width - 8.0;
    const QString title = painter->fontMetrics().elidedText(m_node->name, Qt::ElideMiddle,
                                                            static_cast<int>(titleRight - 8.0));
    painter->drawText(QRectF(8, 0, titleRight - 8, HeaderH), Qt::AlignVCenter, title);

    // Collapse / expand toggle
    if (m_hasChildren) {
        painter->setBrush(headerText);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(m_toggleRect, 3, 3);
        painter->setPen(QPen(header, 2));
        const QPointF c = m_toggleRect.center();
        painter->drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
        if (m_collapsed)
            painter->drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
    }

    // File listing
    painter->setFont(QFont(painter->font().family()));
    QFont body2 = painter->font();
    body2.setBold(false);
    painter->setFont(body2);
    painter->setPen(text);
    qreal y = HeaderH + 4.0;
    const int rows = std::min<int>(m_node->files.size(), MaxFilesShown);
    for (int i = 0; i < rows; ++i) {
        const QString line = painter->fontMetrics().elidedText(m_node->files.at(i), Qt::ElideMiddle,
                                                               static_cast<int>(Width - 16));
        painter->drawText(QRectF(8, y, Width - 16, RowH), Qt::AlignVCenter, line);
        y += RowH;
    }
    if (m_node->fileCount > MaxFilesShown) {
        QColor muted = text;
        muted.setAlpha(150);
        painter->setPen(muted);
        painter->drawText(QRectF(8, y, Width - 16, RowH), Qt::AlignVCenter,
                          QStringLiteral("+%1 more").arg(m_node->fileCount - MaxFilesShown));
    }

    // Border
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(border, isSelected() ? 2.0 : 1.0));
    painter->drawPath(body);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (m_hasChildren && m_toggleRect.contains(event->pos())) {
        m_onToggle(m_node);
        event->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(event);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && m_scene)
        m_scene->onNodeMoved();
    return QGraphicsItem::itemChange(change, value);
}

} // namespace ui
