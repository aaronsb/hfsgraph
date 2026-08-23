// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canvasview.h"

#include "graphscene.h"

#include <cmath>

#include <QFocusEvent>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QBrush>
#include <QPainter>
#include <QPixmap>
#include <QTransform>
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
    if (auto *gs = qobject_cast<GraphScene *>(scene()))
        gs->noteInteraction(); // fast path for the zoom burst (interaction LOD)
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

void CanvasView::endPan() {
    m_panning = false;
    m_panButton = Qt::NoButton;
    if (m_spaceHeld)
        setCursor(Qt::OpenHandCursor);
    else
        unsetCursor();
}

bool CanvasView::isBackgroundAt(const QPoint &viewPos) const {
    // Items that accept no mouse buttons (callout hulls, the move-drag overlay)
    // are passive decoration: a press there would fall through to the scene
    // anyway, so treat them as background and let the pan start.
    const QGraphicsItem *item = itemAt(viewPos);
    return item == nullptr || item->acceptedMouseButtons() == Qt::NoButton;
}

void CanvasView::mousePressEvent(QMouseEvent *event) {
    if (m_panning) {
        // A second button mid-pan must not re-target m_panButton; swallow it.
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        beginPan(event);
        return;
    }
    // Left button pans too when Space is held, or when the press lands on empty
    // background (no item to select or drag there).
    if (event->button() == Qt::LeftButton && (m_spaceHeld || isBackgroundAt(event->pos()))) {
        beginPan(event);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *event) {
    // Two quick pan presses must not read as a cell double-click.
    if (m_spaceHeld || m_panning) {
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent *event) {
    // Losing focus means we will miss the Space release (and possibly the mouse
    // release), so drop the whole pan state rather than leave a stuck mode.
    m_spaceHeld = false;
    if (m_panning)
        endPan();
    else
        unsetCursor();
    QGraphicsView::focusOutEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event) {
    if (m_panning) {
        const QPoint d = event->pos() - m_panLast;
        m_panLast = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        if (auto *gs = qobject_cast<GraphScene *>(scene())) {
            gs->noteInteraction(); // fast path while the pan is in flight
            gs->refreshCallouts(); // keep callouts anchored while panning
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_panning && event->button() == m_panButton) {
        endPan();
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

    // One dot rendered into a tile, then the visible rect filled with it as a texture
    // brush: a single fill instead of one drawEllipse per dot, which at a 4K viewport
    // was thousands of antialiased ellipses per frame. The tile is in device pixels
    // (constant on-screen dot size); the brush transform maps it back to scene units
    // so the grid stays anchored to the scene as the view pans.
    const int tilePx = std::max(2, static_cast<int>(std::lround(spacing * scale)));
    static QPixmap tile;
    static int tileKey = -1;
    const int key = tilePx * 1000 + dot.rgba() % 1000;
    if (tileKey != key || tile.isNull()) {
        tile = QPixmap(tilePx, tilePx);
        tile.fill(Qt::transparent);
        QPainter tp(&tile);
        tp.setRenderHint(QPainter::Antialiasing, true);
        tp.setPen(Qt::NoPen);
        tp.setBrush(dot);
        tp.drawEllipse(QPointF(0.0, 0.0), 1.4, 1.4);
        tp.end();
        tileKey = key;
    }
    QBrush brush(tile);
    const qreal unit = tilePx / scale; // one tile in scene units (≈ spacing)
    brush.setTransform(QTransform::fromScale(unit / tilePx, unit / tilePx));
    painter->setPen(Qt::NoPen);
    painter->setBrush(brush);
    painter->setBrushOrigin(QPointF(0.0, 0.0));
    painter->drawRect(rect);
}

} // namespace ui
