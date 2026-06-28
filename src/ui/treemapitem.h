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
class GroupStore;
}

namespace ui {

class GraphScene;
class FrameItem;

class TreemapItem : public QGraphicsItem {
  public:
    // What a cell's area is proportional to: number of files in the subtree, or
    // bytes on disk in the subtree (classic disk-usage map).
    enum SizeMetric { Files, Bytes };

    // Colour ramp that maps nesting depth to a cell's identity colour. The first
    // entries are the standard perceptually-uniform data-viz ramps; Spectrum is a
    // categorical HSL hue cycle. Keep in sync with kRampNames in the .cpp.
    enum Ramp { Viridis, Magma, Plasma, Cividis, Turbo, Spectrum };

    // Identity colour for a nesting depth under a ramp (depth spans 0..6). Shared so
    // the group panel's depth legend matches what the treemap paints.
    static QColor depthColor(Ramp ramp, int depth);

    TreemapItem(const core::FsNode *root, qreal width, qreal height, SizeMetric metric, Ramp ramp,
                GraphScene *scene);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // Multiplier on the on-screen size at which a cell subdivides: <1 reveals more
    // detail sooner, >1 holds detail back. Paint-only — no rebuild needed.
    void setLod(qreal factor);

    // Re-squarify into new bounds (ADR-304). Larger bounds give each cell more
    // scene-space area, so constant-size labels elide less — the resize/magnify
    // capability. Paint-time layout, so this is just a geometry change + repaint.
    void setSize(qreal width, qreal height);

    // The cell rect (item coords) for a node, computed by replaying the squarify
    // layout from the root down to it — so callouts can re-anchor to a source square
    // after the treemap is resized/moved. Empty if the node isn't under this root.
    QRectF cellRectForNode(const core::FsNode *target) const;

    // The semantic-group overlay source (ADR-102), not owned. Per-cell membership
    // drives highlight (tint + group-colour border), focus (dim non-members), and
    // dim (de-emphasise members). Null = no overlay. Paint-only.
    void setGroupStore(const core::GroupStore *store);

    // The frame that owns this treemap (null for the base map). Lets a double-click
    // tell the scene which frame spawned the new one, for close-cascade lineage.
    void setOwnerFrame(FrameItem *frame) { m_ownerFrame = frame; }

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;       // select
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override; // open a frame

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
    FrameItem *m_ownerFrame = nullptr;          // owning frame, or null for the base map
    const core::GroupStore *m_groups = nullptr; // overlay source (ADR-102), not owned
    const core::FsNode *m_selected = nullptr;
    qreal m_lod = 1.0;            // detail gate multiplier (view distance); <1 = farther
    mutable bool m_dark = true;   // resolved from the palette each paint
    mutable bool m_anyFocus = false; // any visible group in focus mode (resolved each paint)
    mutable qreal m_lastZoom = 1.0;  // view zoom from the last paint (for cellRectForNode)

    mutable std::unordered_map<const core::FsNode *, double> m_weight;
    mutable std::vector<Cell> m_cells; // from the last paint, for hit-testing
};

} // namespace ui
