#include "graphscene.h"

#include "core/fsnode.h"
#include "core/scanner.h"
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
// Hard cap on a lens's own scan depth (relative to its root). Lenses deepen with
// nesting (baseDepth + level); this bounds the recursion/work no matter how deep
// you stack them — "we are talking C++ here".
constexpr int kMaxLensDepth = 12;
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

void GraphScene::updateGroupOverlay() {
    if (m_treemap)
        m_treemap->update();
    for (FrameItem *f : m_frames)
        f->update(); // frames carry the same overlay
}

void GraphScene::openFrame(const core::FsNode *node, const QRectF &originSceneRect,
                           FrameItem *parentFrame) {
    if (!node)
        return;
    const int level = parentFrame ? parentFrame->level() + 1 : 1;

    // Cardinality 1 (default): a node has at most one frame — re-opening raises the
    // existing one instead of stacking a duplicate (ADR-304). Match by path so it
    // works across the base tree and the lenses' own (independently-scanned) trees.
    if (m_uniqueFrames) {
        for (FrameItem *f : m_frames)
            if (f->node() && f->node()->path == node->path) {
                // Re-point the existing frame at the new origin/lineage so its callout
                // and close-cascade stay accurate when re-opened from a different cell.
                f->setParentFrame(parentFrame);
                if (CalloutItem *c = f->callout())
                    c->setSource(node, parentFrame);
                raiseFrame(f);
                return;
            }
    }

    // A lens scans its OWN subtree deeper than the base (baseDepth + level, capped),
    // so deeper lenses reveal more detail. The scan is independent of the shared base
    // tree (no mutation → no dangling), and the frame owns it (RAII, no leak).
    const int depth = std::clamp(m_baseDepth + level, 1, kMaxLensDepth);
    std::unique_ptr<core::FsNode> tree = core::Scanner::scan(node->path, depth);
    const core::FsNode *render = tree ? tree.get() : node; // fall back to the shallow node

    constexpr qreal kFrameW = 520.0, kFrameH = 360.0;
    auto *frame = new FrameItem(render, kFrameW, kFrameH, this);
    frame->adoptTree(std::move(tree)); // sole owner of the deep scan
    frame->setLevel(level);
    frame->setParentFrame(parentFrame); // lineage for the close-cascade
    // Float to the lower-right of the origin so it reads as an enlargement of it
    // without fully covering the source square.
    frame->setPos(originSceneRect.right() + 60.0, originSceneRect.top() + 30.0);
    // The callout's origin is the source node in the source surface (parentFrame's
    // interior, or the base map when parentFrame is null) — tracked dynamically.
    auto *callout = new CalloutItem(node, parentFrame, frame);
    frame->setCallout(callout);
    addItem(callout);
    addItem(frame);
    callout->refresh(); // compute the origin now that it's in the scene
    m_frames.push_back(frame);
    restackFrames(); // assign z so each callout sits just under its frame
    updateSceneBounds(); // a frame may extend past the map's edges
}

void GraphScene::closeFrame(FrameItem *frame) {
    // Idempotent: if the frame isn't (or is no longer) tracked, do nothing. The
    // close is deferred (QTimer) and can also arrive via a parent's cascade or a
    // double-click, so closeFrame may be invoked more than once for the same frame —
    // or after rebuild() already cleared it. The membership check is a pointer
    // compare (no deref), so it is safe even if `frame` is already deleted.
    const auto it = std::find(m_frames.begin(), m_frames.end(), frame);
    if (it == m_frames.end())
        return;
    m_frames.erase(it); // remove before recursing so a re-entrant call can't re-find it

    // Close descendants first (frames opened from within this one), so closing an
    // upstream frame never leaves its children dangling.
    std::vector<FrameItem *> children;
    for (FrameItem *f : m_frames)
        if (f->parentFrame() == frame)
            children.push_back(f);
    for (FrameItem *c : children)
        closeFrame(c);

    if (CalloutItem *c = frame->callout()) {
        removeItem(c);
        delete c; // a plain item; deferred close means we're never in its own handler
    }
    removeItem(frame);
    frame->deleteLater();
}

void GraphScene::raiseFrame(FrameItem *frame) {
    if (std::find(m_frames.begin(), m_frames.end(), frame) == m_frames.end())
        return;
    // Raise the frame *and its descendants* together, preserving their relative
    // order (a child is always created after its parent, so the order already has
    // ancestors before descendants). Raising the whole subtree keeps child frames —
    // and their × — above the frame the user clicked, never buried beneath it.
    auto inSubtree = [&](FrameItem *f) {
        for (FrameItem *p = f; p; p = p->parentFrame())
            if (p == frame)
                return true;
        return false;
    };
    std::vector<FrameItem *> rest, sub;
    for (FrameItem *f : m_frames)
        (inSubtree(f) ? sub : rest).push_back(f);
    m_frames = std::move(rest);
    m_frames.insert(m_frames.end(), sub.begin(), sub.end());
    restackFrames();
}

void GraphScene::refreshCallouts() {
    // Every callout — for a view change (zoom/pan), where all of them shift.
    for (FrameItem *f : m_frames)
        if (CalloutItem *c = f->callout())
            c->refresh();
}

void GraphScene::refreshCalloutsFor(FrameItem *frame) {
    // Only the callouts a move/resize of `frame` affects: its own (its frame-end
    // moved) and any whose origin lives inside it (child lenses sourced from it).
    // Grandchildren are sourced from unmoved frames, so they need no refresh.
    if (CalloutItem *c = frame->callout())
        c->refresh();
    for (FrameItem *g : m_frames)
        if (CalloutItem *c = g->callout())
            if (c->sourceFrame() == frame)
                c->refresh();
}

void GraphScene::setCalloutMode(int mode) {
    m_calloutMode = mode;
    refreshCallouts(); // repaint all in the new mode
}

void GraphScene::restackFrames() {
    // Two z-slots per frame: callout just below its frame, frames in stack order,
    // all above the base map (z 0).
    qreal z = 100.0;
    for (FrameItem *f : m_frames) {
        if (CalloutItem *c = f->callout())
            c->setZValue(z);
        f->setZValue(z + 1.0);
        z += 2.0;
    }
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
