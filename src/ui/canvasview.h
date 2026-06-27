// The viewport: wheel-zoom, drag-to-pan, and a dotted-grid background that gives
// the canvas a sense of physical space (ADR-300 design language).
#pragma once

#include <QGraphicsView>

namespace ui {

class CanvasView : public QGraphicsView {
    Q_OBJECT
  public:
    explicit CanvasView(QWidget *parent = nullptr);

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
};

} // namespace ui
