#include "graphscene.h"

#include "core/fsnode.h"
#include "nodeitem.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPalette>
#include <QPen>

namespace ui {

namespace {
constexpr qreal kTwoPi = 2.0 * 3.14159265358979323846;
constexpr qreal kNodeR = 190.0; // base disk radius reserved for a single node
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

// Balloon layout pass 1: size each subtree's bounding disk. A leaf reserves
// kNodeR; an internal node sizes a ring big enough to seat its children's disks
// around it, then its own disk is that ring plus the largest child disk.
qreal GraphScene::computeRadius(const core::FsNode *node,
                                std::unordered_map<const core::FsNode *, qreal> &ringR,
                                std::unordered_map<const core::FsNode *, qreal> &subR) const {
    if (isCollapsed(node) || node->children.empty()) {
        subR[node] = kNodeR;
        return kNodeR;
    }
    qreal sum = 0.0, maxChild = 0.0;
    for (const auto &child : node->children) {
        const qreal cr = computeRadius(child.get(), ringR, subR);
        sum += 2.0 * cr;
        maxChild = std::max(maxChild, cr);
    }
    const qreal R = std::max(sum / kTwoPi, kNodeR + maxChild);
    ringR[node] = R;
    const qreal sr = R + maxChild;
    subR[node] = sr;
    return sr;
}

// Balloon layout pass 2: place each child around its parent's ring, giving each
// an angular slice proportional to its subtree disk so siblings cluster locally
// and fill 2D space (no hollow global ring).
void GraphScene::placeBalloon(const core::FsNode *node, QPointF center, qreal baseAngle,
                              const std::unordered_map<const core::FsNode *, qreal> &ringR,
                              const std::unordered_map<const core::FsNode *, qreal> &subR,
                              std::unordered_map<const core::FsNode *, QPointF> &pos) const {
    pos[node] = center;
    if (isCollapsed(node) || node->children.empty())
        return;
    const qreal R = ringR.at(node);
    qreal total = 0.0;
    for (const auto &child : node->children)
        total += 2.0 * subR.at(child.get());
    qreal a = baseAngle;
    for (const auto &child : node->children) {
        const qreal frac = (2.0 * subR.at(child.get())) / total;
        const qreal mid = a + frac * kTwoPi / 2.0;
        const QPointF c = center + QPointF(R * std::cos(mid), R * std::sin(mid));
        placeBalloon(child.get(), c, mid + kTwoPi / 2.0, ringR, subR, pos);
        a += frac * kTwoPi;
    }
}

void GraphScene::rebuild() {
    clear(); // deletes all items (nodes + edges + effects)
    m_items.clear();
    m_edges.clear();
    if (!m_root)
        return;

    std::unordered_map<const core::FsNode *, qreal> ringR, subR;
    computeRadius(m_root, ringR, subR);
    std::unordered_map<const core::FsNode *, QPointF> pos;
    placeBalloon(m_root, QPointF(0, 0), 0.0, ringR, subR, pos);

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
