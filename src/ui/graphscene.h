// Hosts the treemap view of a core::FsNode tree. The graph is a strict containment
// tree (ADR-101), so we render it as a squarified treemap (ADR-300's nested-
// containment view) rather than node-link cards + edges: nesting *is* the
// parent→child relationship, and semantic (level-of-detail) zoom reveals depth as
// you zoom in — no fixed render depth. GraphScene owns the root and drill
// navigation; TreemapItem does the layout and painting.
#pragma once

#include <QGraphicsScene>

#include "core/group.h"

#include <vector>

namespace core {
struct FsNode;
}

namespace ui {

class TreemapItem;
class FrameItem;

class GraphScene : public QGraphicsScene {
    Q_OBJECT
  public:
    explicit GraphScene(QObject *parent = nullptr);

    // Set the tree to display (not owned). Triggers a rebuild.
    void setRoot(const core::FsNode *root);
    const core::FsNode *root() const { return m_root; }

    // Treemap appearance (indices match TreemapItem::SizeMetric / ::Ramp). Each
    // rebuilds the map.
    void setSizeMetric(int metric); // 0 = file count, 1 = bytes
    void setColorRamp(int ramp);    // Viridis/Magma/Plasma/Cividis/Turbo/Spectrum
    void setLod(double factor);     // detail "view distance" (live; no rebuild)

    int sizeMetric() const { return m_sizeMetric; } // current metric (for frames)
    double lod() const { return m_lod; }            // current LOD factor (for frames)

    // Investigation frames (ADR-303), the replacement for re-root/drill. openFrame
    // floats a new frame rooted at `node`, anchored to `originSceneRect` via callout
    // lines (a non-destructive lens — the base is never re-rooted); closeFrame
    // removes it; raiseFrame brings it to the front of the frame stack.
    void openFrame(const core::FsNode *node, const QRectF &originSceneRect,
                   FrameItem *parentFrame = nullptr);
    void closeFrame(FrameItem *frame); // also closes frames opened from within it
    void raiseFrame(FrameItem *frame);

    // Semantic groups (ADR-102). The store is resolved against the *scan* root on
    // setRoot (so drill navigation doesn't disturb membership) and owned here; the
    // panel reads/edits it and calls updateGroupOverlay() to repaint the treemap.
    core::GroupStore &groups() { return m_groups; }
    void updateGroupOverlay(); // repaint the overlay after a group view-state change
    int colorRamp() const { return m_colorRamp; } // current ramp (for the depth legend)

  private:
    void rebuild();
    void updateSceneBounds(); // generous sceneRect so panning works in all directions
    void restackFrames();     // reassign z so each callout sits just under its frame

    const core::FsNode *m_root = nullptr;     // currently displayed (sub)tree
    const core::FsNode *m_scanRoot = nullptr; // full scanned tree (group resolution)
    core::GroupStore m_groups;                // semantic groups (ADR-102), owned
    TreemapItem *m_treemap = nullptr;         // current item (for live LOD tuning)
    std::vector<FrameItem *> m_frames;        // open investigation frames (ADR-303)
    int m_sizeMetric = 0;             // TreemapItem::Files
    int m_colorRamp = 0;              // TreemapItem::Viridis
    double m_lod = 1.0;               // persists across rebuilds
};

} // namespace ui
