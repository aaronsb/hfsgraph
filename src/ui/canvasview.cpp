#include "canvasview.h"

#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QWheelEvent>

namespace ui {

CanvasView::CanvasView(QWidget *parent) : QGraphicsView(parent) {
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    // NoDrag: the treemap fills the viewport and the left button is for cell
    // select / (future) drag-to-reparent, so panning is on the middle button
    // (handled below) instead of QGraphicsView's left-button ScrollHandDrag.
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Repaint the whole viewport each frame. The default MinimalViewportUpdate
    // derives dirty regions from item boundingRects, but our nodes paint a drop
    // shadow well outside their boundingRect — so an animating (moving) node left a
    // stale "ghost" of the prior frame's shadow, reading as a second superimposed
    // node at the timer rate. Full updates clear it. (A mouse drag already forced
    // full repaints, which is why holding a node hid the artifact.)
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

void CanvasView::wheelEvent(QWheelEvent *event) {
    constexpr double step = 1.15;
    const double factor = event->angleDelta().y() > 0 ? step : 1.0 / step;
    scale(factor, factor);
}

void CanvasView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panLast = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event) {
    if (m_panning) {
        const QPoint d = event->pos() - m_panLast;
        m_panLast = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::drawBackground(QPainter *painter, const QRectF &rect) {
    const QColor base = palette().color(QPalette::Base).darker(112);
    painter->fillRect(rect, base);

    // Adapt grid spacing to zoom so dot density stays roughly constant on screen
    // (dots became far too fine when zoomed out). Dot radius is divided by the scale
    // so it stays a constant size in device pixels.
    const qreal scale = transform().m11();
    if (scale <= 0.0)
        return;
    qreal spacing = 40.0;
    while (spacing * scale < 22.0) // too dense on screen -> coarsen
        spacing *= 2.0;
    while (spacing * scale > 90.0) // too sparse -> refine
        spacing /= 2.0;

    QColor dot = palette().color(QPalette::Mid);
    dot.setAlpha(110);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot);

    const qreal dotR = 1.4 / scale; // ~constant on-screen dot size
    const qreal left = std::floor(rect.left() / spacing) * spacing;
    const qreal top = std::floor(rect.top() / spacing) * spacing;
    for (qreal x = left; x < rect.right(); x += spacing)
        for (qreal y = top; y < rect.bottom(); y += spacing)
            painter->drawEllipse(QPointF(x, y), dotR, dotR);
}

} // namespace ui
