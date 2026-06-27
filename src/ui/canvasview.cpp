#include "canvasview.h"

#include <cmath>

#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

namespace ui {

CanvasView::CanvasView(QWidget *parent) : QGraphicsView(parent) {
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void CanvasView::wheelEvent(QWheelEvent *event) {
    constexpr double step = 1.15;
    const double factor = event->angleDelta().y() > 0 ? step : 1.0 / step;
    scale(factor, factor);
}

void CanvasView::drawBackground(QPainter *painter, const QRectF &rect) {
    const QColor base = palette().color(QPalette::Base).darker(112);
    painter->fillRect(rect, base);

    constexpr int grid = 40;
    QColor dot = palette().color(QPalette::Mid);
    dot.setAlpha(90);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot);

    const qreal left = std::floor(rect.left() / grid) * grid;
    const qreal top = std::floor(rect.top() / grid) * grid;
    for (qreal x = left; x < rect.right(); x += grid)
        for (qreal y = top; y < rect.bottom(); y += grid)
            painter->drawEllipse(QPointF(x, y), 1.3, 1.3);
}

} // namespace ui
