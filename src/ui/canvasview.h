// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// The viewport: wheel-zoom, drag-to-pan, and a dotted-grid background that
// gives the canvas a sense of physical space (ADR-300 design language).
// Panning starts from the middle button, from the left button while Space is
// held (open-hand cursor signals the mode), or from the left button on empty
// background. A left press on a cell is left alone because the treemap fills
// the viewport and that button is reserved for cell select / drag-to-reparent.
#pragma once

#include <QGraphicsView>
#include <QPoint>

namespace ui {

class CanvasView : public QGraphicsView {
    Q_OBJECT
  public:
    explicit CanvasView(QWidget *parent = nullptr);

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

  private:
    void beginPan(QMouseEvent *event);

    bool m_panning = false;
    bool m_spaceHeld = false;
    Qt::MouseButton m_panButton = Qt::NoButton; // which button started the pan
    QPoint m_panLast;
};

} // namespace ui
