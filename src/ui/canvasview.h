// The viewport: wheel-zoom, middle-button drag-to-pan, and a dotted-grid
// background that gives the canvas a sense of physical space (ADR-300 design
// language). Panning is on the middle button because the treemap fills the
// viewport and the left button is reserved for cell select / drag-to-reparent.
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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

  private:
    bool m_panning = false;
    QPoint m_panLast;
};

} // namespace ui
