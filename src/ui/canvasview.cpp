// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canvasview.h"

#include "graphscene.h"

#include <cmath>

#include <QKeyEvent>
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
    // select / (future) drag-to-reparent, so panning is handled below (middle
    // button, Space+left, or left on empty background) instead of
    // QGraphicsView's left-button ScrollHandDrag.
    setDragMode(QGraphicsView::NoDrag);
    // Needed to receive the Space key for the Space+drag pan mode.
    setFocusPolicy(Qt::StrongFocus);
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
    if (auto *gs = qobject_cast<GraphScene *>(scene()))
        gs->refreshCallouts(); // keep investigation-frame callouts anchored on zoom
}

void CanvasView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        if (!m_panning)
            setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        if (!m_panning)
            unsetCursor(); // an in-flight pan keeps its closed hand until release
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void CanvasView::beginPan(QMouseEvent *event) {
    m_panning = true;
    m_panButton = event->button();
    m_panLast = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        beginPan(event);
        return;
    }
    // Left button pans too when Space is held, or when the press lands on empty
    // background (no item to select or drag there).
    if (event->button() == Qt::LeftButton && (m_spaceHeld || itemAt(event->pos()) == nullptr)) {
        beginPan(event);
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
        if (auto *gs = qobject_cast<GraphScene *>(scene()))
            gs->refreshCallouts(); // keep callouts anchored while panning
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_panning && event->button() == m_panButton) {
        m_panning = false;
        m_panButton = Qt::NoButton;
        if (m_spaceHeld)
            setCursor(Qt::OpenHandCursor);
        else
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
