#include "graphscene.h"

#include "core/fsnode.h"
#include "nodeitem.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QTimer>

namespace ui {

namespace {
// d3-style spring-electrical model. Inverse-square charge (repulsion) + Hooke
// springs toward a rest length (attraction), integrated with velocity damping and
// an alpha that cools so the system actually settles (no limit cycle).
constexpr double kL0 = 300.0;         // spring rest length (ideal edge length)
constexpr double kCharge = 30000.0;   // repulsion strength (inverse-square)
constexpr double kSpring = 0.35;      // spring stiffness (Hooke)
constexpr double kDamp = 0.60;        // velocity retained per step (d3 velocityDecay 0.4)
constexpr double kAlphaDecay = 0.985; // cooling per step
constexpr double kReheat = 0.7;       // alpha restored on interaction
constexpr double kMinDist = 8.0;      // clamp so charge doesn't blow up at d→0
constexpr double kMaxStep = 80.0;     // safety cap (should be unused once settled)
constexpr int kIterations = 320;      // build-time convergence passes
} // namespace

GraphScene::GraphScene(QObject *parent) : QGraphicsScene(parent) {}

bool GraphScene::isCollapsed(const core::FsNode *node) const {
    return m_collapsed.find(node) != m_collapsed.end();
}

void GraphScene::setRoot(const core::FsNode *root) {
    m_root = root;
    m_collapsed.clear();
    rebuild();
}

void GraphScene::toggleCollapse(const core::FsNode *node) {
    if (isCollapsed(node))
        m_collapsed.erase(node);
    else
        m_collapsed.insert(node);
    rebuild();
}

void GraphScene::collectVisible(const core::FsNode *node,
                                std::vector<const core::FsNode *> &out) const {
    out.push_back(node);
    if (isCollapsed(node))
        return;
    for (const auto &child : node->children)
        collectVisible(child.get(), out);
}

// Seed the simulation: phyllotaxis spiral positions (even, deterministic spread),
// file-count mass, parent↔child edges, and a starting temperature.
void GraphScene::seedSim() {
    const int n = static_cast<int>(m_simNodes.size());
    m_simPos.assign(n, QPointF());
    m_simVel.assign(n, QPointF());
    m_simMass.assign(n, 1.0);
    m_simEdges.clear();

    std::unordered_map<const core::FsNode *, int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i)
        idx[m_simNodes[i]] = i;

    constexpr double golden = 2.39996322972865332;
    for (int i = 0; i < n; ++i) {
        const double r = kL0 * 0.6 * std::sqrt(static_cast<double>(i));
        const double a = i * golden;
        m_simPos[i] = QPointF(r * std::cos(a), r * std::sin(a));
        // Mass = total objects in the dir: files + child dirs (drives repulsion).
        const double objects =
            m_simNodes[i]->fileCount + static_cast<double>(m_simNodes[i]->children.size());
        m_simMass[i] = 1.0 + std::log2(1.0 + objects);
    }
    for (int i = 0; i < n; ++i) {
        if (!m_simNodes[i]->parent)
            continue;
        auto it = idx.find(m_simNodes[i]->parent);
        if (it != idx.end())
            m_simEdges.emplace_back(i, it->second);
    }
    m_alpha = 1.0; // full energy for the initial convergence
}

// One spring-electrical pass (d3-style). Inverse-square charge repels every pair
// (weighted by file-count mass); Hooke springs pull each edge toward a rest length
// kL0 (so far children aren't yanked in like rigid rods). Forces are scaled by the
// cooling alpha, added to velocity, which is then damped — so it settles instead of
// limit-cycling at a step cap. The pinned (dragged) node is never moved by the sim.
double GraphScene::simIterate() {
    const int n = static_cast<int>(m_simPos.size());
    if (n == 0)
        return 0.0;
    std::vector<QPointF> force(n, QPointF(0, 0));

    const double minD2 = kMinDist * kMinDist;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double dx = m_simPos[i].x() - m_simPos[j].x();
            double dy = m_simPos[i].y() - m_simPos[j].y();
            double d2 = dx * dx + dy * dy;
            if (d2 < minD2) {
                if (d2 < 1e-6) { // coincident — deterministic nudge
                    dx = (i - j);
                    dy = 0.37;
                    d2 = dx * dx + dy * dy;
                }
                d2 = std::max(d2, minD2);
            }
            const double d = std::sqrt(d2);
            const double mag = m_repulsion * kCharge * m_simMass[i] * m_simMass[j] / d2;
            const QPointF u(dx / d * mag, dy / d * mag);
            force[i] += u;
            force[j] -= u;
        }
    }
    for (const auto &e : m_simEdges) {
        double dx = m_simPos[e.first].x() - m_simPos[e.second].x();
        double dy = m_simPos[e.first].y() - m_simPos[e.second].y();
        const double d = std::sqrt(std::max(dx * dx + dy * dy, 1.0));
        const double mag = m_attraction * kSpring * (d - kL0); // Hooke toward rest length
        const QPointF u(dx / d * mag, dy / d * mag);
        force[e.first] -= u;
        force[e.second] += u;
    }

    double maxStep = 0.0;
    for (int i = 0; i < n; ++i) {
        if (i == m_draggedIndex) {
            m_simVel[i] = QPointF(0, 0);
            continue;
        }
        QPointF v(m_simVel[i].x() + force[i].x() * m_alpha,
                  m_simVel[i].y() + force[i].y() * m_alpha);
        v *= kDamp;
        double vl = std::sqrt(v.x() * v.x() + v.y() * v.y());
        if (vl > kMaxStep) {
            v *= kMaxStep / vl;
            vl = kMaxStep;
        }
        m_simVel[i] = v;
        m_simPos[i] += v;
        maxStep = std::max(maxStep, vl);
    }
    m_alpha *= kAlphaDecay; // cool toward rest
    return maxStep;
}

void GraphScene::settle(int iters) {
    for (int i = 0; i < iters; ++i)
        simIterate();
}

void GraphScene::writePositions() {
    m_suppressEdges = true; // move every node first, refresh edges once at the end
    for (int i = 0; i < static_cast<int>(m_simNodes.size()); ++i) {
        if (i == m_draggedIndex)
            continue; // the user owns the dragged node's position
        auto it = m_items.find(m_simNodes[i]);
        if (it == m_items.end())
            continue;
        NodeItem *item = it->second;
        item->setPos(m_simPos[i].x() - item->nodeWidth() / 2.0,
                     m_simPos[i].y() - item->nodeHeight() / 2.0);
    }
    m_suppressEdges = false;
    refreshEdges();
}

void GraphScene::stepPhysicsTick() {
    if (m_simNodes.empty())
        return;
    // The dragged node is authoritative: read its live position back into the sim
    // so forces respond to where the user put it, but the sim never moves it.
    if (m_draggedIndex >= 0 && m_draggedIndex < static_cast<int>(m_simNodes.size())) {
        auto it = m_items.find(m_simNodes[m_draggedIndex]);
        if (it != m_items.end()) {
            NodeItem *item = it->second;
            m_simPos[m_draggedIndex] =
                QPointF(item->x() + item->nodeWidth() / 2.0, item->y() + item->nodeHeight() / 2.0);
            m_simVel[m_draggedIndex] = QPointF(0, 0);
        }
    }
    simIterate();
    writePositions();
}

void GraphScene::rebuild() {
    clear(); // deletes all items (nodes + edges + effects)
    m_items.clear();
    m_edges.clear();
    m_draggedIndex = -1; // items are recreated; no live drag survives a rebuild
    if (!m_root)
        return;

    m_simNodes.clear();
    collectVisible(m_root, m_simNodes);
    seedSim();
    settle(kIterations); // converge to a clustered layout for the initial view

    for (int i = 0; i < static_cast<int>(m_simNodes.size()); ++i) {
        const core::FsNode *node = m_simNodes[i];
        const bool hasChildren = !node->children.empty();
        auto *item = new NodeItem(
            node, hasChildren, isCollapsed(node),
            [this](const core::FsNode *n) { toggleCollapse(n); }, this);
        item->setPos(m_simPos[i].x() - item->nodeWidth() / 2.0,
                     m_simPos[i].y() - item->nodeHeight() / 2.0);
        item->setZValue(1.0);

        auto *shadow = new QGraphicsDropShadowEffect();
        shadow->setBlurRadius(18.0);
        shadow->setOffset(0.0, 4.0);
        shadow->setColor(QColor(0, 0, 0, 110));
        item->setGraphicsEffect(shadow);

        addItem(item);
        m_items.emplace(node, item);
    }

    for (const auto &[node, item] : m_items) {
        if (!node->parent)
            continue;
        auto pit = m_items.find(node->parent);
        if (pit == m_items.end())
            continue;
        auto *edge = new QGraphicsPathItem();
        edge->setZValue(0.0);
        // Contrasty + cosmetic pen: cosmetic keeps a constant device width so edges
        // stay visible when zoomed out instead of fading to a sub-pixel hairline.
        QColor ec = qApp ? qApp->palette().color(QPalette::Text) : QColor(210, 210, 210);
        ec.setAlpha(90); // visible but not glaring
        QPen epen(ec, 1.4);
        epen.setCosmetic(true); // constant device width so it doesn't vanish when zoomed out
        edge->setPen(epen);
        addItem(edge);
        m_edges.push_back({pit->second, item, edge});
    }

    refreshEdges();
    updateSceneBounds();

    if (m_physicsOn)
        setPhysicsRunning(true); // reheat & keep animating across rebuilds
}

void GraphScene::setPhysicsRunning(bool on) {
    m_physicsOn = on;
    if (on) {
        m_alpha = std::max(m_alpha, kReheat); // give it energy to move, then it cools to rest
        if (!m_timer) {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, [this] { stepPhysicsTick(); });
        }
        m_timer->start(30);
    } else if (m_timer) {
        m_timer->stop();
    }
}

void GraphScene::setRepulsion(double k) {
    m_repulsion = k;
    m_alpha = std::max(m_alpha, kReheat); // re-settle under the new force
}

void GraphScene::setAttraction(double k) {
    m_attraction = k;
    m_alpha = std::max(m_alpha, kReheat);
}

void GraphScene::setDragged(const core::FsNode *node) {
    m_draggedIndex = -1;
    for (int i = 0; i < static_cast<int>(m_simNodes.size()); ++i) {
        if (m_simNodes[i] == node) {
            m_draggedIndex = i;
            break;
        }
    }
}

void GraphScene::clearDragged() {
    if (m_draggedIndex >= 0 && m_draggedIndex < static_cast<int>(m_simNodes.size())) {
        auto it = m_items.find(m_simNodes[m_draggedIndex]);
        if (it != m_items.end()) {
            NodeItem *item = it->second;
            m_simPos[m_draggedIndex] =
                QPointF(item->x() + item->nodeWidth() / 2.0, item->y() + item->nodeHeight() / 2.0);
            m_simVel[m_draggedIndex] = QPointF(0, 0);
        }
    }
    m_draggedIndex = -1;
    m_alpha = std::max(m_alpha, kReheat); // let neighbors re-settle around the dropped node
}

void GraphScene::setAllShaded(bool shaded) {
    m_suppressEdges = true;
    for (auto &[node, item] : m_items)
        item->setShaded(shaded);
    m_suppressEdges = false;
    refreshEdges();
    updateSceneBounds();
}

void GraphScene::setAllViewMode(int mode) {
    for (auto &[node, item] : m_items)
        item->setViewMode(mode);
}

void GraphScene::fitAllToContent() {
    m_suppressEdges = true;
    for (auto &[node, item] : m_items)
        item->fitToContent();
    m_suppressEdges = false;
    refreshEdges();
    updateSceneBounds();
}

void GraphScene::refreshEdges() {
    for (const Edge &e : m_edges) {
        const QPointF s =
            e.from->pos() + QPointF(e.from->nodeWidth() / 2.0, e.from->nodeHeight() / 2.0);
        const QPointF t = e.to->pos() + QPointF(e.to->nodeWidth() / 2.0, e.to->nodeHeight() / 2.0);
        QPainterPath path(s);
        path.lineTo(t);
        e.item->setPath(path);
    }
}

void GraphScene::onNodeMoved() {
    if (m_suppressEdges)
        return;
    refreshEdges();
}

void GraphScene::updateSceneBounds() {
    // Pad the content rect by at least its own size (min 2000px) on every side so
    // ScrollHandDrag can pan freely in all directions and well beyond the graph.
    const QRectF b = itemsBoundingRect();
    const qreal m = std::max({2000.0, b.width(), b.height()});
    setSceneRect(b.adjusted(-m, -m, m, m));
}

} // namespace ui
