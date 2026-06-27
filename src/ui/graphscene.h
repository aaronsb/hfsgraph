// Builds and lays out the canvas from a core::FsNode tree: one NodeItem per
// visible directory, containment edges between them, and a top-down tidy-tree
// layout. Owns collapse/expand state (UI state — kept out of the core model).
#pragma once

#include <QGraphicsScene>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QGraphicsPathItem;

namespace core {
struct FsNode;
}

namespace ui {

class NodeItem;

class GraphScene : public QGraphicsScene {
    Q_OBJECT
  public:
    explicit GraphScene(QObject *parent = nullptr);

    // Set the tree to display (not owned). Triggers a rebuild.
    void setRoot(const core::FsNode *root);

    // Collapse/expand toggle for one node, then relayout.
    void toggleCollapse(const core::FsNode *node);

    // Called by NodeItems when dragged, to keep edges attached.
    void onNodeMoved();

  private:
    void rebuild();
    // Balloon layout: each subtree is a local disk centered on its parent. Pass 1
    // sizes every subtree disk; pass 2 places nodes around their parent's ring.
    qreal computeRadius(const core::FsNode *node,
                        std::unordered_map<const core::FsNode *, qreal> &ringR,
                        std::unordered_map<const core::FsNode *, qreal> &subR) const;
    void placeBalloon(const core::FsNode *node, QPointF center, qreal baseAngle,
                      const std::unordered_map<const core::FsNode *, qreal> &ringR,
                      const std::unordered_map<const core::FsNode *, qreal> &subR,
                      std::unordered_map<const core::FsNode *, QPointF> &pos) const;
    void refreshEdges();
    bool isCollapsed(const core::FsNode *node) const;

    struct Edge {
        NodeItem *from;
        NodeItem *to;
        QGraphicsPathItem *item;
    };

    const core::FsNode *m_root = nullptr;
    std::unordered_set<const core::FsNode *> m_collapsed;
    std::unordered_map<const core::FsNode *, NodeItem *> m_items;
    std::vector<Edge> m_edges;
};

} // namespace ui
