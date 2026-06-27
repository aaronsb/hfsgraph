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

// Fruchterman-Reingold force-directed layout, iterated to convergence. Seeded on
// a phyllotaxis spiral (even spread, deterministic), then: all-pairs repulsion
// (scaled by sqrt of file-count mass so heavy dirs claim more room) pushes nodes
// apart, edge springs pull parent↔child together, and a cooling schedule lets the
// whole thing settle into clusters instead of a frozen ring.
void GraphScene::forceLayout(const std::vector<const core::FsNode *> &nodes,
                             std::unordered_map<const core::FsNode *, QPointF> &pos) const {
    const int n = static_cast<int>(nodes.size());
    if (n == 0)
        return;

    std::unordered_map<const core::FsNode *, int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i)
        idx[nodes[i]] = i;

    std::vector<QPointF> p(n);
    std::vector<double> mass(n);
    constexpr double golden = 2.39996322972865332;
    for (int i = 0; i < n; ++i) {
        const double r = kIdeal * 0.55 * std::sqrt(static_cast<double>(i));
        const double a = i * golden;
        p[i] = QPointF(r * std::cos(a), r * std::sin(a));
        mass[i] = 1.0 + std::log2(1.0 + nodes[i]->fileCount);
    }

    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < n; ++i) {
        if (!nodes[i]->parent)
            continue;
        auto it = idx.find(nodes[i]->parent);
        if (it != idx.end())
            edges.emplace_back(i, it->second);
    }

    std::vector<QPointF> disp(n);
    double t = kIdeal * 1.5; // initial temperature (max step), cooled each pass
    for (int iter = 0; iter < kIterations; ++iter) {
        std::fill(disp.begin(), disp.end(), QPointF(0, 0));

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double dx = p[i].x() - p[j].x();
                double dy = p[i].y() - p[j].y();
                double d2 = dx * dx + dy * dy;
                if (d2 < 1.0) { // coincident — nudge apart deterministically
                    dx = (i - j);
                    dy = 0.37;
                    d2 = dx * dx + dy * dy;
                }
                const double d = std::sqrt(d2);
                const double f = (kIdeal * kIdeal / d) * std::sqrt(mass[i] * mass[j]);
                const QPointF u(dx / d * f, dy / d * f);
                disp[i] += u;
                disp[j] -= u;
            }
        }

        for (const auto &e : edges) {
            double dx = p[e.first].x() - p[e.second].x();
            double dy = p[e.first].y() - p[e.second].y();
            const double d = std::sqrt(std::max(dx * dx + dy * dy, 1.0));
            const double f = (d * d) / kIdeal;
            const QPointF u(dx / d * f, dy / d * f);
            disp[e.first] -= u;
            disp[e.second] += u;
        }

        for (int i = 0; i < n; ++i) {
            const double dl = std::sqrt(disp[i].x() * disp[i].x() + disp[i].y() * disp[i].y());
            if (dl > 1e-6) {
                const double c = std::min(dl, t);
                p[i] += QPointF(disp[i].x() / dl * c, disp[i].y() / dl * c);
            }
        }
        t *= 0.985; // cool
    }

    QPointF centroid(0, 0);
    for (int i = 0; i < n; ++i)
        centroid += p[i];
    centroid /= static_cast<double>(n);
    for (int i = 0; i < n; ++i)
        pos[nodes[i]] = p[i] - centroid;
}

void GraphScene::rebuild() {
    clear(); // deletes all items (nodes + edges + effects)
    m_items.clear();
    m_edges.clear();
    if (!m_root)
        return;

    std::vector<const core::FsNode *> nodes;
    collectVisible(m_root, nodes);
    std::unordered_map<const core::FsNode *, QPointF> pos;
    forceLayout(nodes, pos);

    for (const auto &[node, p] : pos) {
        const bool hasChildren = !node->children.empty();
        auto *item = new NodeItem(
            node, hasChildren, isCollapsed(node),
            [this](const core::FsNode *n) { toggleCollapse(n); }, this);
        item->setPos(p.x() - item->nodeWidth() / 2.0, p.y() - item->nodeHeight() / 2.0);
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
    setSceneRect(itemsBoundingRect().adjusted(-200, -200, 200, 200));
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
    refreshEdges();
}

} // namespace ui
