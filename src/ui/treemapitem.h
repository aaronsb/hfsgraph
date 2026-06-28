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

    // How a cell's files are drawn. Auto picks by cell size (list → icons → dots as
    // it shrinks); the others force that rung regardless of size (ADR-301). List is a
    // multi-column icon+name grid (like `ls -a`); Details is one file per row with
    // metadata (perms/size/mtime), like `ls -l` (force-only — never auto-picked, it
    // needs the most room). Keep in sync with the toolbar combo order in MainWindow.
    enum FileMode { Auto, Dots, Icons, List, Details };

    // Identity colour for a nesting depth under a ramp (depth spans 0..6). Shared so
    // the group panel's depth legend matches what the treemap paints.
    static QColor depthColor(Ramp ramp, int depth);

    TreemapItem(const core::FsNode *root, qreal width, qreal height, SizeMetric metric, Ramp ramp,
                GraphScene *scene);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // Two independent level-of-detail multipliers on the on-screen sizes at which
    // things appear (ADR-301). They were one knob, but they fought: "Reveal" gates
    // how deep cells *subdivide* (nesting shown), while "Detail" gates the *contents
    // crossover* — at what cell size a leaf switches pixel-dots → icons → name. <1
    // triggers sooner (more), >1 holds back. Paint-only — no rebuild needed.
    void setReveal(qreal factor); // subdivision / nesting depth
    void setDetail(qreal factor); // contents rung crossover (dots/icons/name)

    // Force the file rung (FileMode), or Auto to pick by size. Paint-only.
    void setFileMode(int mode);

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
    // The leaf rung (files as names / icons / dots, or the cell's own name) — split
    // out of drawCell to keep it focused on cull / subdivide / chrome.
    void drawLeafContents(QPainter *painter, const core::FsNode *node, const QRectF &dev,
                          bool hasTitle, const QColor &body) const;
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
    qreal m_reveal = 1.0;        // subdivision gate multiplier; <1 = subdivide sooner
    qreal m_detail = 1.0;        // contents-crossover gate multiplier; <1 = icons/name sooner
    int m_fileMode = Auto;       // forced file rung, or Auto (size-driven)
    mutable bool m_dark = true;   // resolved from the palette each paint
    mutable bool m_anyFocus = false; // any visible group in focus mode (resolved each paint)
    mutable qreal m_lastZoom = 1.0;  // view zoom from the last paint (for cellRectForNode)

    mutable std::unordered_map<const core::FsNode *, double> m_weight;
    mutable std::vector<Cell> m_cells; // from the last paint, for hit-testing
};

} // namespace ui
