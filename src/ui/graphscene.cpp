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
constexpr double kIdeal = 330.0; // ideal edge length / node separation
constexpr int kIterations = 450;
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
    m_simMass.assign(n, 1.0);
    m_simEdges.clear();

    std::unordered_map<const core::FsNode *, int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i)
        idx[m_simNodes[i]] = i;

    constexpr double golden = 2.39996322972865332;
    for (int i = 0; i < n; ++i) {
        const double r = kIdeal * 0.55 * std::sqrt(static_cast<double>(i));
        const double a = i * golden;
        m_simPos[i] = QPointF(r * std::cos(a), r * std::sin(a));
        m_simMass[i] = 1.0 + std::log2(1.0 + m_simNodes[i]->fileCount);
    }
    for (int i = 0; i < n; ++i) {
        if (!m_simNodes[i]->parent)
            continue;
        auto it = idx.find(m_simNodes[i]->parent);
        if (it != idx.end())
            m_simEdges.emplace_back(i, it->second);
    }
    m_simTemp = kIdeal * 1.5;
}

// One Fruchterman-Reingold pass over the sim state. All-pairs repulsion (scaled by
// sqrt of file-count mass so heavy dirs claim more room), edge springs, capped by
// temperature, which then cools. Returns the largest single-node displacement.
double GraphScene::simIterate() {
    const int n = static_cast<int>(m_simPos.size());
    if (n == 0)
        return 0.0;
    std::vector<QPointF> disp(n, QPointF(0, 0));

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double dx = m_simPos[i].x() - m_simPos[j].x();
            double dy = m_simPos[i].y() - m_simPos[j].y();
            double d2 = dx * dx + dy * dy;
            if (d2 < 1.0) { // coincident — nudge apart deterministically
                dx = (i - j);
                dy = 0.37;
                d2 = dx * dx + dy * dy;
            }
            const double d = std::sqrt(d2);
            const double f = (kIdeal * kIdeal / d) * std::sqrt(m_simMass[i] * m_simMass[j]);
            const QPointF u(dx / d * f, dy / d * f);
            disp[i] += u;
            disp[j] -= u;
        }
    }
    for (const auto &e : m_simEdges) {
        double dx = m_simPos[e.first].x() - m_simPos[e.second].x();
        double dy = m_simPos[e.first].y() - m_simPos[e.second].y();
        const double d = std::sqrt(std::max(dx * dx + dy * dy, 1.0));
        const double f = (d * d) / kIdeal;
        const QPointF u(dx / d * f, dy / d * f);
        disp[e.first] -= u;
        disp[e.second] += u;
    }

    double maxDisp = 0.0;
    for (int i = 0; i < n; ++i) {
        const double dl = std::sqrt(disp[i].x() * disp[i].x() + disp[i].y() * disp[i].y());
        if (dl > 1e-6) {
            const double c = std::min(dl, m_simTemp);
            m_simPos[i] += QPointF(disp[i].x() / dl * c, disp[i].y() / dl * c);
            maxDisp = std::max(maxDisp, c);
        }
    }
    m_simTemp *= 0.985; // cool
    return maxDisp;
}

void GraphScene::settle(int iters) {
    for (int i = 0; i < iters; ++i)
        simIterate();
}

void GraphScene::writePositions() {
    m_suppressEdges = true; // move every node first, refresh edges once at the end
    for (int i = 0; i < static_cast<int>(m_simNodes.size()); ++i) {
        auto it = m_items.find(m_simNodes[i]);
        if (it == m_items.end())
            continue;
        NodeItem *item = it->second;
        item->setPos(m_simPos[i].x() - item->nodeWidth() / 2.0,
                     m_simPos[i].y() - item->nodeHeight() / 2.0);
    }
    m_suppressEdges = false;
    refreshEdges();
    setSceneRect(itemsBoundingRect().adjusted(-300, -300, 300, 300));
}

void GraphScene::stepPhysicsTick() {
    if (m_simNodes.empty())
        return;
    simIterate();
    writePositions();
}

void GraphScene::rebuild() {
    clear(); // deletes all items (nodes + edges + effects)
    m_items.clear();
    m_edges.clear();
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
        edge->setPen(
            QPen(qApp ? qApp->palette().color(QPalette::Mid) : QColor(120, 120, 120), 1.5));
        addItem(edge);
        m_edges.push_back({pit->second, item, edge});
    }

    refreshEdges();
    setSceneRect(itemsBoundingRect().adjusted(-300, -300, 300, 300));

    if (m_physicsOn)
        setPhysicsRunning(true); // reheat & keep animating across rebuilds
}

void GraphScene::setPhysicsRunning(bool on) {
    m_physicsOn = on;
    if (on) {
        m_simTemp = kIdeal * 0.7; // reheat so motion is visible
        if (!m_timer) {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, [this] { stepPhysicsTick(); });
        }
        m_timer->start(30);
    } else if (m_timer) {
        m_timer->stop();
    }
}

void GraphScene::setAllShaded(bool shaded) {
    m_suppressEdges = true;
    for (auto &[node, item] : m_items)
        item->setShaded(shaded);
    m_suppressEdges = false;
    refreshEdges();
    setSceneRect(itemsBoundingRect().adjusted(-300, -300, 300, 300));
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
    setSceneRect(itemsBoundingRect().adjusted(-300, -300, 300, 300));
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

} // namespace ui
