// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

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
// the exposed viewport rect, so every zoom frame re-evaluates detail and every frame
// culls off-screen cells. Geometry itself is cached in a TreemapLayout keyed on zoom,
// so pans and plain repaints don't re-squarify. Pure view; no structural authority.
#pragma once

#include "treemaplayout.h"

#include <QGraphicsItem>
#include <QHash>
#include <QRectF>
#include <QString>

#include <memory>
#include <unordered_map>

class QVariantAnimation;

namespace core {
struct FsNode;
class GroupStore;
} // namespace core

namespace ui {

class GraphScene;
class FrameItem;
struct LeafPlan; // a leaf cell's rung + glyph geometry (treemapitem.cpp)

class TreemapItem : public QGraphicsItem {
  public:
    // What a cell's area is proportional to: number of files in the subtree, or
    // bytes on disk in the subtree (classic disk-usage map).
    enum SizeMetric { Files, Bytes };

    // Colour ramp that maps nesting depth to a cell's identity colour. The first
    // entries are the standard perceptually-uniform data-viz ramps; Spectrum is a
    // categorical HSL hue cycle. Keep in sync with kRampNames in the .cpp.
    enum Ramp { Viridis, Magma, Plasma, Cividis, Turbo, Spectrum };

    // How a cell's files are drawn. The rungs, richest → poorest: Details is one file
    // per row with metadata (perms/size/mtime), like `ls -l` (force-only — never
    // auto-picked, it needs the most room); List is a multi-column icon+name grid
    // (like `ls -a`); IconsNamed is an icon with its name elided beneath, grid-packed;
    // Icons is the bare icon grid; Dots is the pixel-dot density grid. Auto picks the
    // richest rung that fits the cell (list → icon+name → icons → dots as it shrinks);
    // a forced mode prefers its rung and falls down the table to the next one that
    // fits instead of drawing nothing (ADR-301, #44); the table's size gates are Auto-only,
    // a forced rung is gated by its painter's own fit alone. The rung table lives in
    // the .cpp. Keep in sync with the toolbar combo order in MainWindow: the combo is
    // a prefix of this enum, so Icons — the Auto-only intermediate the toolbar never
    // offers ("Files: Icons" means IconsNamed) — sits last, past the combo.
    enum FileMode { Auto, Dots, IconsNamed, List, Details, Icons };

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

    // Interaction LOD: while a zoom/pan gesture is in flight the leaf rungs (names,
    // icons, dots) are skipped — they're the bulk of a large frame's raster cost and
    // unreadable mid-motion anyway. The scene sets this for the gesture and clears it
    // after a short idle, which repaints at full detail. Paint-only.
    void setInteracting(bool on);

    // Re-squarify into new bounds (ADR-304). Larger bounds give each cell more
    // scene-space area, so constant-size labels elide less — the resize/magnify
    // capability. Paint-time layout, so this is just a geometry change + repaint.
    void setSize(qreal width, qreal height);

    // The cell rect (item coords) for a node — from the layout cache, or replayed
    // for a node too small to have a cell — so callouts can re-anchor to a source
    // square after the treemap is resized/moved. Empty if the node isn't under this root.
    QRectF cellRectForNode(const core::FsNode *target) const;

    // The geometry this item paints from (ADR-301): a cache keyed on panel size,
    // metric, LOD factors and zoom. Hit-testing and callouts read it; the coming
    // file-glyph hit-test will too.
    const TreemapLayout &layout() const { return m_layout; }
    // The tree changed underneath (a deepened subtree): drop the cached geometry.
    void invalidateLayout();

    // Layout focus (#40, spike). Decided in paint() from the current zoom and
    // viewport: once one cell covers kFocusEngage of the viewport on both axes it
    // becomes the focus — its subtree is re-squarified into the viewport's rect over
    // the canonical map, its ancestors reduced to breadcrumb frames (TreemapLayout).
    // It pops to its parent when it shrinks under kFocusRelease (hysteresis), and
    // clears at the root. The change is told to the scene (setLayoutFocus) and the
    // old→new cell rects are blended over a short animation so the eye can follow.
    static constexpr double kFocusEngage = 0.6;
    static constexpr double kFocusRelease = 0.45;
    const core::FsNode *layoutFocus() const { return m_focus; }
    ~TreemapItem() override;

    // The semantic-group overlay source (ADR-102), not owned. Per-cell membership
    // drives highlight (tint + group-colour border), focus (dim non-members), and
    // dim (de-emphasise members). Null = no overlay. Paint-only.
    void setGroupStore(const core::GroupStore *store);

    // The frame that owns this treemap (null for the base map). Lets a double-click
    // tell the scene which frame spawned the new one, for close-cascade lineage.
    void setOwnerFrame(FrameItem *frame) { m_ownerFrame = frame; }

    // The deepest cell under an item-space point, for the scene's move-drag target
    // hit-testing (#10). Null if no cell is hit.
    const core::FsNode *cellNodeAt(const QPointF &itemPos) const { return cellAt(itemPos); }

    // The file glyph under an item-space point (#37): the leaf directory and the
    // index into its `files`, or {null, -1}. Reads the same rung geometry paint uses.
    std::pair<const core::FsNode *, int> fileAt(const QPointF &itemPos) const;

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;        // select / arm drag
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;         // drive the move drag
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;      // drop / commit
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;  // open a frame
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override; // file / dir actions
    // Drop target (#38): the selection dragged out of any surface and dropped on a
    // directory cell stages a MoveFile per entry. Foreign URLs are ignored.
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent *event) override;
    void dropEvent(QGraphicsSceneDragDropEvent *event) override;

  private:
    // The morph (#40): the rect a cell is drawn at is lerp(from, layout rect, t). `from`
    // is the rect the same node showed before the focus change, else its new rect
    // carried into its parent's from-rect proportionally (newly revealed cells grow
    // out of their parent). The parent's pair is passed down the drawCell walk.
    struct MorphParent {
        QRectF from, to; // the parent's shown-from rect and its layout rect
    };
    // Paint one layout cell (by index) and, if it subdivided, its children.
    void drawCell(QPainter *painter, int index, const QTransform &toDevice, const QRectF &exposed,
                  const MorphParent *parent) const;
    // Re-evaluate the layout focus against the viewport (device rect `vpDev`, item
    // rect `vpItem`); on a change, snapshot the shown rects, switch, and rebuild.
    void decideFocus(TreemapLayout::Params &lp, const QRectF &vpDev, const QRectF &vpItem,
                     const QTransform &toDevice);
    void setFocus(const core::FsNode *focus, const QRectF &focusRect,
                  const TreemapLayout::Params &lp);
    QRectF morphFrom(const LayoutCell &cell, const MorphParent *parent) const;
    double morphT() const; // eased animation progress, 1 when idle
    // The rung and glyph geometry of a leaf cell's files (LeafPlan, in the .cpp);
    // shared by painting, the file hit-test and band selection.
    LeafPlan planLeaf(const core::FsNode *node, const QRectF &dev, bool hasTitle) const;
    // The leaf rung (files as names / icons / dots, or the cell's own name) — split
    // out of drawCell to keep it focused on cull / subdivide / chrome.
    // `visibleDev` is the exposed region in device px. Each rung lays its grid over
    // the whole cell body and draws only the on-screen window of it: zooming into a
    // directory reads as being inside it, panning reveals more of the same
    // arrangement, and off-screen glyphs cost nothing.
    void drawLeafContents(QPainter *painter, const core::FsNode *node, const QRectF &dev,
                          bool hasTitle, const QColor &body, const QRectF &visibleDev) const;
    // The staged-move diff decoration (ADR-302 #12): a crosshatch + a step-number badge
    // marking a square the plan relocated, drawn on top of the cell (and its children).
    void drawDiffMark(QPainter *painter, const QRectF &dev, int step) const;
    // The "children not scanned yet" hatch on a truncated leaf (lazy deepening, #36).
    void drawUnscannedMark(QPainter *painter, const QRectF &dev) const; // dev = body rect
    // Rubber-band selection (#37): the band in item coords while one is being dragged.
    void drawBand(QPainter *painter) const;
    // The glyph index under a point within a leaf cell, in that cell's own device
    // space (origin = the cell's top-left at the current zoom), or -1.
    int fileIndexIn(const LayoutCell &cell, const QPointF &itemPos) const;
    // Every file index of the leaf `node` (cell rect `cellRect`) whose glyph rect
    // intersects `itemRect` (band select).
    std::vector<int> filesIn(const core::FsNode *node, const QRectF &cellRect, bool hasTitle,
                             const QRectF &itemRect) const;
    void endFileGesture(); // clear press/band state and release the scene's graft hold
    // The plan step that actually relocated `node` (0 = none) — gates drawDiffMark so a
    // skipped op never paints a false mark on a node still in its original place.
    int diffStepFor(const core::FsNode *node) const;
    const core::FsNode *cellAt(const QPointF &p) const; // deepest cell under a point

    TreemapLayout m_layout; // cached geometry (see layout())

    // Layout focus state (#40).
    const core::FsNode *m_focus = nullptr;
    QRectF m_focusRect; // outermost frame rect, item coords
    std::unique_ptr<QVariantAnimation> m_morph;
    std::unordered_map<const core::FsNode *, QRectF> m_fromCanon;   // canonical cells
    std::unordered_map<const core::FsNode *, QRectF> m_fromOverlay; // overlay cells

    const core::FsNode *m_root;
    qreal m_w;
    qreal m_h;
    SizeMetric m_metric;
    Ramp m_ramp;
    GraphScene *m_scene;
    FrameItem *m_ownerFrame = nullptr;          // owning frame, or null for the base map
    const core::GroupStore *m_groups = nullptr; // overlay source (ADR-102), not owned
    const core::FsNode *m_selected = nullptr;
    // File gesture state (#37). A press on a file glyph arms a file drag-out
    // (text/uri-list); a press on a leaf's file area off any glyph arms a rubber band.
    const core::FsNode *m_pressFileDir = nullptr; // directory of the pressed glyph
    int m_pressFileIndex = -1;                    // its index, -1 = no file pressed
    // The band's cell is held by value: the layout's cell vector is rebuilt by a zoom
    // step or a landing deepen, so a pointer into it wouldn't survive the gesture.
    const core::FsNode *m_bandNode = nullptr; // leaf a band is being dragged in
    QRectF m_bandRect;                        // that cell's rect, item coords
    bool m_bandHasTitle = false;
    QPointF m_bandOrigin;            // band anchor, item coords
    QRectF m_band;                   // live band, item coords (null = none)
    bool m_pressWasSelected = false; // Ctrl-press on a selected glyph: toggle on release
    // Move-drag gesture state (#10). A press on a movable cell arms a drag; once the
    // cursor leaves a small threshold the scene-level overlay takes over (the scene
    // owns the arrow + cross-surface target hit-testing, since a drop can land on a
    // different base). These are cleared on release.
    const core::FsNode *m_pressNode = nullptr; // cell pressed (candidate drag source)
    QPointF m_pressScene;                      // press position, scene coords (threshold)
    QPointF m_pressCenterScene;                // pressed cell centre, scene coords (arrow tail)
    bool m_dragging = false;                   // past the threshold: a drag is live
    qreal m_reveal = 1.0;                      // subdivision gate multiplier; <1 = subdivide sooner
    qreal m_detail = 1.0;            // contents-crossover gate multiplier; <1 = icons/name sooner
    int m_fileMode = Auto;           // forced file rung, or Auto (size-driven)
    bool m_interacting = false;      // gesture in flight: skip leaf contents
    mutable bool m_dark = true;      // resolved from the palette each paint
    mutable bool m_anyFocus = false; // any visible group in focus mode (resolved each paint)
    // identity → 1-based ledger step for cells the staged plan relocated (#12); rebuilt
    // each paint from the scene's active ops, so the overlay tracks queue scrub/undo.
    mutable QHash<QString, int> m_diffSteps;
};

} // namespace ui
