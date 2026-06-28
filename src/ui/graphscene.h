// Hosts the treemap view of a core::FsNode tree. The graph is a strict containment
// tree (ADR-101), so we render it as a squarified treemap (ADR-300's nested-
// containment view) rather than node-link cards + edges: nesting *is* the
// parent→child relationship, and semantic (level-of-detail) zoom reveals depth as
// you zoom in — no fixed render depth. GraphScene owns the root and drill
// navigation; TreemapItem does the layout and painting.
#pragma once

#include <QGraphicsScene>

namespace core {
struct FsNode;
}

namespace ui {

class TreemapItem;

class GraphScene : public QGraphicsScene {
    Q_OBJECT
  public:
    explicit GraphScene(QObject *parent = nullptr);

    // Set the tree to display (not owned). Triggers a rebuild.
    void setRoot(const core::FsNode *root);
    const core::FsNode *root() const { return m_root; }

    // Re-root navigation (treemap double-click drills in; the toolbar's Up ascends).
    void drillInto(const core::FsNode *node); // descend into a child directory
    void drillUp();                           // ascend to the parent, if any

    // Treemap appearance (indices match TreemapItem::SizeMetric / ::Ramp). Each
    // rebuilds the map.
    void setSizeMetric(int metric); // 0 = file count, 1 = bytes
    void setColorRamp(int ramp);    // Viridis/Magma/Plasma/Cividis/Turbo/Spectrum
    void setLod(double factor);     // detail "view distance" (live; no rebuild)

  private:
    void rebuild();
    void updateSceneBounds(); // generous sceneRect so panning works in all directions

    const core::FsNode *m_root = nullptr;
    TreemapItem *m_treemap = nullptr; // current item (for live LOD tuning)
    int m_sizeMetric = 0;             // TreemapItem::Files
    int m_colorRamp = 0;              // TreemapItem::Viridis
    double m_lod = 1.0;               // persists across rebuilds
};

} // namespace ui
