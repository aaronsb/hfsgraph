#include "graphscene.h"

#include "core/fsnode.h"
#include "treemapitem.h"

#include <algorithm>

#include <QRectF>

namespace ui {

namespace {
// The treemap's root rectangle in scene units. Absolute size is arbitrary — the
// view zoom decides on-screen scale — but a 16:10 frame reads well.
constexpr qreal kCanvasW = 1600.0;
constexpr qreal kCanvasH = 1000.0;
} // namespace

GraphScene::GraphScene(QObject *parent) : QGraphicsScene(parent) {}

void GraphScene::setRoot(const core::FsNode *root) {
    m_root = root;
    rebuild();
}

void GraphScene::drillInto(const core::FsNode *node) {
    if (node && !node->children.empty())
        setRoot(node); // re-root onto the subtree (parent pointers let drillUp return)
}

void GraphScene::drillUp() {
    if (m_root && m_root->parent)
        setRoot(m_root->parent);
}

void GraphScene::setSizeMetric(int metric) {
    m_sizeMetric = metric;
    rebuild();
}

void GraphScene::setColorRamp(int ramp) {
    m_colorRamp = ramp;
    rebuild();
}

void GraphScene::setLod(double factor) {
    m_lod = factor;
    if (m_treemap)
        m_treemap->setLod(factor); // live — paint-only, no rebuild
}

void GraphScene::rebuild() {
    clear(); // deletes all items, including the previous treemap
    m_treemap = nullptr;
    if (!m_root)
        return;
    m_treemap = new TreemapItem(m_root, kCanvasW, kCanvasH,
                                static_cast<TreemapItem::SizeMetric>(m_sizeMetric),
                                static_cast<TreemapItem::Ramp>(m_colorRamp), this);
    m_treemap->setLod(m_lod);
    addItem(m_treemap);
    updateSceneBounds();
}

void GraphScene::updateSceneBounds() {
    // Pad the content so ScrollHandDrag can pan past the map's edges.
    const QRectF b = itemsBoundingRect();
    const qreal m = std::max({600.0, b.width() * 0.5, b.height() * 0.5});
    setSceneRect(b.adjusted(-m, -m, m, m));
}

} // namespace ui
