// Builds and lays out the canvas from a core::FsNode tree: one NodeItem per
// visible directory, containment edges between them, and a force-directed layout.
// Owns collapse/expand state and the (optionally animated) physics simulation.
#pragma once

#include <QGraphicsScene>
#include <QPointF>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class QTimer;
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

    // Experimental toolbar controls.
    void setPhysicsRunning(bool on); // animate the force sim live
    bool physicsRunning() const { return m_physicsOn; }
    void setRepulsion(double k); // live force tuning
    void setAttraction(double k);
    void setAllShaded(bool shaded); // bulk window-shade open/close
    void setAllViewMode(int mode);  // bulk icons (0) / list (1)
    void fitAllToContent();         // size every node to its object count

    // Pin the node under an active drag so the sim doesn't fight/snap it back.
    void setDragged(const core::FsNode *node);
    void clearDragged();

  private:
    void rebuild();
    void collectVisible(const core::FsNode *node, std::vector<const core::FsNode *> &out) const;
    // Force-directed (Fruchterman-Reingold) simulation, steppable so it can run to
    // convergence at build or animate live. Repulsion is weighted by file-count mass.
    void seedSim();
    double simIterate();    // one pass; returns max displacement; cools temperature
    void settle(int iters); // run simIterate iters times (build-time convergence)
    void writePositions();  // sim positions -> item positions (+ edge refresh)
    void stepPhysicsTick(); // one animated step
    void refreshEdges();
    void updateSceneBounds(); // generous sceneRect so panning works in all directions
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

    // Force-sim state (indices align with m_simNodes).
    std::vector<const core::FsNode *> m_simNodes;
    std::vector<QPointF> m_simPos;
    std::vector<QPointF> m_simVel; // per-node velocity (damped integrator)
    std::vector<double> m_simMass;
    std::vector<std::pair<int, int>> m_simEdges;
    double m_repulsion = 1.0;
    double m_attraction = 1.0;
    double m_alpha = 1.0;         // cooling factor; decays so the sim settles, reheated on demand
    int m_draggedIndex = -1;      // node currently held by the user (pinned), or -1
    bool m_suppressEdges = false; // batch guard: refresh edges once, not per move
    bool m_physicsOn = false;
    QTimer *m_timer = nullptr;
};

} // namespace ui
