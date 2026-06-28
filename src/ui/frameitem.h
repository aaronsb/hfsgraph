// An investigation frame (ADR-303): a floating panel, living in the *same* scene
// as the base treemap, that holds its own squarified treemap rooted at a
// double-clicked subtree. It does not re-root the base — it's a non-destructive,
// stackable lens. The frame has a draggable header (move / close), an interior
// treemap child (semantic LOD + the group overlay apply within it), and casts an
// ordered-dither drop shadow (the project's future-retro dither language).
//
// Frames are tied to their origin square by callout lines and can be opened
// recursively (a double-click inside a frame opens a deeper frame) — both wired in
// later Slice-2 tasks. Frames root on node identity, so they survive moves and
// queue-scrubs (ADR-302/303).
#pragma once

#include <QGraphicsObject>
#include <QPointF>
#include <QRectF>

#include <memory>

namespace core {
struct FsNode;
}

namespace ui {

class GraphScene;
class TreemapItem;
class FrameItem;
class ResizeGrip;

// The "zoom-into" visual tie between an origin square and its frame (ADR-303):
// two diagonal hairlines, origin upper-right → frame upper-right and origin
// lower-left → frame lower-left, drawn just *below* the frame (and above the base
// map) so the frame reads as an enlargement of that region. Works in scene
// coordinates (placed at the origin); it reads the frame's live position so it
// follows the frame as it is dragged.
class CalloutItem : public QGraphicsItem {
  public:
    CalloutItem(const QRectF &originSceneRect, FrameItem *frame);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    void refresh();                            // call when the frame moves
    void setOrigin(const QRectF &originSceneRect); // re-point at a new origin square

  private:
    QRectF m_origin;     // origin square, scene coordinates
    FrameItem *m_frame;  // the frame this callout points to (not owned)
};

// QGraphicsObject (not plain QGraphicsItem) so a frame can deleteLater() itself
// when its × is clicked — safe deletion from within its own event handler.
class FrameItem : public QGraphicsObject {
    Q_OBJECT
  public:
    FrameItem(const core::FsNode *node, qreal width, qreal height, GraphScene *scene);
    ~FrameItem() override; // defined in the .cpp so the unique_ptr<FsNode> can free it

    // Take ownership of the deep-scanned subtree this frame renders (ADR-304 /
    // per-level depth). The frame is the sole owner — RAII frees it on close/rebuild,
    // so the independent per-lens scan never leaks.
    void adoptTree(std::unique_ptr<core::FsNode> tree);

    int level() const { return m_level; }   // 1 for a top lens, +1 per nesting
    void setLevel(int level) { m_level = level; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    const core::FsNode *node() const { return m_node; }
    void setLod(qreal factor); // forward to the interior treemap

    // Resize the panel + re-squarify the interior (ADR-304), clamped to a minimum.
    // Driven by the corner ResizeGrip; gives every cell more room for its label.
    void resizePanel(qreal width, qreal height);

    QSizeF panelSize() const; // panel (body) size in item units, for callout anchoring
    void setCallout(CalloutItem *callout) { m_callout = callout; }
    CalloutItem *callout() const { return m_callout; }

    // The frame this one was opened from (null for a top-level frame). Closing a
    // frame cascade-closes its descendants so none are left dangling (ADR-303).
    void setParentFrame(FrameItem *parent) { m_parentFrame = parent; }
    FrameItem *parentFrame() const { return m_parentFrame; }

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    // Click-to-raise: a press anywhere in the frame (including its interior treemap,
    // a child item) brings the frame to the front, without swallowing the event so
    // the interior still selects / double-clicks (which opens a recursive frame).
    bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;

  private:
    QRectF panelRect() const;    // the frame body (excludes the shadow margin)
    QRectF headerRect() const;   // draggable title strip
    QRectF closeRect() const;    // the × hit box
    QRectF interiorRect() const; // where the child treemap sits

    const core::FsNode *m_node;
    qreal m_w;
    qreal m_h;
    GraphScene *m_scene;
    TreemapItem *m_interior = nullptr;
    CalloutItem *m_callout = nullptr;     // tie to the origin square (not owned)
    FrameItem *m_parentFrame = nullptr;   // frame that spawned this one (not owned)
    ResizeGrip *m_grip = nullptr;         // bottom-right resize handle (child item)
    bool m_dragging = false;
    QPointF m_dragOffset;   // cursor → item-origin offset while dragging
    qreal m_lastZoom = 1.0; // view zoom from the last paint (for device-aligned closeRect)
    int m_level = 1;        // lens nesting level (1 = top lens); deepens the scan
    std::unique_ptr<core::FsNode> m_ownTree; // the frame's own deep scan (owned; RAII)
};

// Bottom-right corner handle that resizes its frame. A child item (so it grabs the
// mouse cleanly, above the interior treemap) drawn at a constant screen size
// (ItemIgnoresTransformations) anchored at the panel's corner.
class ResizeGrip : public QGraphicsItem {
  public:
    explicit ResizeGrip(FrameItem *frame);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

  private:
    FrameItem *m_frame;
    QPointF m_startScene;
    QSizeF m_startSize;
    bool m_active = false;
};

} // namespace ui
