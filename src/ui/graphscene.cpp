#include "graphscene.h"

#include "core/fsnode.h"
#include "frameitem.h"
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
    m_scanRoot = root; // a genuinely new tree: (re)resolve rule groups against it
    if (m_scanRoot)
        core::resolveRuleGroups(*m_scanRoot, m_groups);
    else
        m_groups.clear(); // cleared canvas: drop stale groups so the panel matches
    rebuild();
}

void GraphScene::drillInto(const core::FsNode *node) {
    // Re-root the *view* onto a subtree (parent pointers let drillUp return). Groups
    // stay resolved against the scan root, so membership is unaffected by drilling.
    if (node && !node->children.empty()) {
        m_root = node;
        rebuild();
    }
}

void GraphScene::drillUp() {
    if (m_root && m_root->parent) {
        m_root = m_root->parent;
        rebuild();
    }
}

void GraphScene::updateGroupOverlay() {
    if (m_treemap)
        m_treemap->update();
    for (FrameItem *f : m_frames)
        f->update(); // frames carry the same overlay
}

void GraphScene::openFrame(const core::FsNode *node, const QPointF &originScenePos) {
    if (!node)
        return;
    constexpr qreal kFrameW = 520.0, kFrameH = 360.0;
    auto *frame = new FrameItem(node, kFrameW, kFrameH, this);
    // Offset from the origin so the frame doesn't fully cover its source square.
    frame->setPos(originScenePos + QPointF(48, 36));
    frame->setZValue(100.0 + static_cast<qreal>(m_frames.size()));
    addItem(frame);
    m_frames.push_back(frame);
    updateSceneBounds(); // a frame may extend past the map's edges
}

void GraphScene::closeFrame(FrameItem *frame) {
    const auto it = std::find(m_frames.begin(), m_frames.end(), frame);
    if (it != m_frames.end())
        m_frames.erase(it);
    removeItem(frame);
    frame->deleteLater(); // safe even when called from the frame's own handler
}

void GraphScene::raiseFrame(FrameItem *frame) {
    const auto it = std::find(m_frames.begin(), m_frames.end(), frame);
    if (it == m_frames.end())
        return;
    m_frames.erase(it);
    m_frames.push_back(frame); // front of the stack
    qreal z = 100.0;
    for (FrameItem *f : m_frames)
        f->setZValue(z++);
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
    for (FrameItem *f : m_frames)
        f->setLod(factor);
}

void GraphScene::rebuild() {
    clear(); // deletes all items, including the previous treemap and any frames
    m_treemap = nullptr;
    m_frames.clear(); // the FrameItems were just deleted by clear()
    if (!m_root)
        return;
    m_treemap = new TreemapItem(m_root, kCanvasW, kCanvasH,
                                static_cast<TreemapItem::SizeMetric>(m_sizeMetric),
                                static_cast<TreemapItem::Ramp>(m_colorRamp), this);
    m_treemap->setLod(m_lod);
    m_treemap->setGroupStore(&m_groups);
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
