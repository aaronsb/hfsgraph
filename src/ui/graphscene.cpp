#include "graphscene.h"

#include "core/fsnode.h"
#include "nodeitem.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPalette>
#include <QPen>

namespace ui {

namespace {
constexpr qreal kXStep = 240.0; // horizontal spacing between leaf columns
constexpr qreal kYStep = 210.0; // vertical spacing between depth levels
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

qreal GraphScene::layout(const core::FsNode *node, int depth, qreal &cursor,
                         std::unordered_map<const core::FsNode *, QPointF> &pos) {
    qreal x;
    if (isCollapsed(node) || node->children.empty()) {
        x = cursor * kXStep;
        cursor += 1.0;
    } else {
        qreal first = 0, last = 0;
        bool firstSet = false;
        for (const auto &child : node->children) {
            const qreal cx = layout(child.get(), depth + 1, cursor, pos);
            if (!firstSet) {
                first = cx;
                firstSet = true;
            }
            last = cx;
        }
        x = (first + last) / 2.0;
    }
    pos[node] = QPointF(x, depth * kYStep);
    return x;
}

void GraphScene::rebuild() {
    clear(); // deletes all items (nodes + edges + effects)
    m_items.clear();
    m_edges.clear();
    if (!m_root)
        return;

    std::unordered_map<const core::FsNode *, QPointF> pos;
    qreal cursor = 0.0;
    layout(m_root, 0, cursor, pos);

    for (const auto &[node, p] : pos) {
        const bool hasChildren = !node->children.empty();
        auto *item = new NodeItem(
            node, hasChildren, isCollapsed(node),
            [this](const core::FsNode *n) { toggleCollapse(n); }, this);
        item->setPos(p.x() - NodeItem::Width / 2.0, p.y());
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
        const qreal sx = e.from->x() + NodeItem::Width / 2.0;
        const qreal sy = e.from->y() + e.from->nodeHeight();
        const qreal ex = e.to->x() + NodeItem::Width / 2.0;
        const qreal ey = e.to->y();
        const qreal midY = (sy + ey) / 2.0;

        QPainterPath path(QPointF(sx, sy));
        path.cubicTo(QPointF(sx, midY), QPointF(ex, midY), QPointF(ex, ey));
        e.item->setPath(path);
    }
}

void GraphScene::onNodeMoved() {
    refreshEdges();
}

} // namespace ui
