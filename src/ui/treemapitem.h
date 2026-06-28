// A squarified treemap of the containment tree (ADR-300's nested-containment
// view): every directory is a rectangle, subdivided among its children with area
// ∝ subtree file count. Nesting *is* the parent→child relationship, so there are
// no edges to route.
//
// Rendering is semantic (level-of-detail) zoom: a cell only subdivides into its
// children when it is large enough *on screen* to be worth it, and labels/icons
// are drawn at a constant screen size (not scaled with the zoom). So zooming in
// reveals deeper structure rather than just enlarging pixels — there is no fixed
// render depth. The recursion lives in paint(), driven by the painter's zoom and
// the exposed viewport rect, so every zoom/pan frame re-evaluates detail and culls
// off-screen cells. Pure view; no structural authority.
#pragma once

#include <QGraphicsItem>
#include <QRectF>
#include <unordered_map>
#include <vector>

namespace core {
struct FsNode;
}

namespace ui {

class GraphScene;

class TreemapItem : public QGraphicsItem {
  public:
    // What a cell's area is proportional to: number of files in the subtree, or
    // bytes on disk in the subtree (classic disk-usage map).
    enum SizeMetric { Files, Bytes };

    // Colour ramp that maps nesting depth to a cell's identity colour. The first
    // entries are the standard perceptually-uniform data-viz ramps; Spectrum is a
    // categorical HSL hue cycle. Keep in sync with kRampNames in the .cpp.
    enum Ramp { Viridis, Magma, Plasma, Cividis, Turbo, Spectrum };

    TreemapItem(const core::FsNode *root, qreal width, qreal height, SizeMetric metric, Ramp ramp,
                GraphScene *scene);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;       // select
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override; // drill in

  private:
    struct Cell {
        QRectF rect;
        const core::FsNode *node;
    };

    double weight(const core::FsNode *n) const; // subtree file count (memoized)
    void drawCell(QPainter *painter, const core::FsNode *node, const QRectF &rect, int depth,
                  const QTransform &toDevice, const QRectF &exposed) const;
    const core::FsNode *cellAt(const QPointF &p) const; // deepest cell under a point

    const core::FsNode *m_root;
    qreal m_w;
    qreal m_h;
    SizeMetric m_metric;
    Ramp m_ramp;
    GraphScene *m_scene;
    const core::FsNode *m_selected = nullptr;
    mutable bool m_dark = true; // resolved from the palette each paint

    mutable std::unordered_map<const core::FsNode *, double> m_weight;
    mutable std::vector<Cell> m_cells; // from the last paint, for hit-testing
};

} // namespace ui
