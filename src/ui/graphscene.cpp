#include "graphscene.h"

#include "core/fsnode.h"
#include "core/scanner.h"
#include "frameitem.h"
#include "treemapitem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QGraphicsView>
#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QTimer>
#include <QTransform>
#include <QWidget>

#include "core/move.h"

namespace ui {

// Top-Z scene overlay for the drag-to-move gesture (#10): a connector from the lifted
// square to the cursor, ending in ▶ (legal drop) or ✕ (illegal), plus a highlight on
// the target cell. The connector + highlight live in scene space (cosmetic pens →
// zoom-constant width); the end markers are drawn device-space so they don't scale.
class MoveDragOverlay : public QGraphicsItem {
  public:
    MoveDragOverlay() {
        setZValue(1e6); // above every frame + callout
        setAcceptedMouseButtons(Qt::NoButton);
        setFlag(ItemIsSelectable, false);
    }
    void setState(const QPointF &src, const QPointF &cursor, const QRectF &targetScene, bool legal) {
        prepareGeometryChange();
        m_src = src;
        m_cursor = cursor;
        m_target = targetScene;
        m_legal = legal;
        update();
    }
    QRectF boundingRect() const override {
        QRectF r = QRectF(m_src, m_cursor).normalized();
        if (!m_target.isNull())
            r = r.united(m_target);
        return r.adjusted(-40, -40, 40, 40); // slack for the device-space markers
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        const QColor col = m_legal ? QColor(90, 210, 130) : QColor(235, 95, 85);
        if (!m_target.isNull()) {
            QPen tp(col, 2.0);
            tp.setCosmetic(true);
            p->setPen(tp);
            QColor fill = col;
            fill.setAlpha(55);
            p->setBrush(fill);
            p->drawRect(m_target);
            p->setBrush(Qt::NoBrush);
        }
        QPen lp(col, 2.5);
        lp.setCosmetic(true);
        p->setPen(lp);
        p->drawLine(m_src, m_cursor);

        // Device-space end markers (constant screen size regardless of zoom).
        const QTransform t = p->worldTransform();
        const QPointF srcDev = t.map(m_src), curDev = t.map(m_cursor);
        p->setWorldMatrixEnabled(false);
        drawCross(p, srcDev, 4.0, col); // the "lifted from here" mark
        if (m_legal)
            drawArrowHead(p, srcDev, curDev, col); // ▶ at the cursor
        else
            drawCross(p, curDev, 6.0, col); // ✕ at the cursor
        p->setWorldMatrixEnabled(true);
    }

  private:
    static void drawCross(QPainter *p, const QPointF &c, qreal r, const QColor &col) {
        QPen pen(col, 2.0);
        p->setPen(pen);
        p->drawLine(c + QPointF(-r, -r), c + QPointF(r, r));
        p->drawLine(c + QPointF(-r, r), c + QPointF(r, -r));
    }
    static void drawArrowHead(QPainter *p, const QPointF &from, const QPointF &tip,
                              const QColor &col) {
        const QLineF l(from, tip);
        const double ang = std::atan2(l.dy(), l.dx());
        constexpr double spread = 0.5, len = 12.0;
        const QPointF a = tip - QPointF(std::cos(ang - spread) * len, std::sin(ang - spread) * len);
        const QPointF b = tip - QPointF(std::cos(ang + spread) * len, std::sin(ang + spread) * len);
        QPolygonF head;
        head << tip << a << b;
        p->setPen(Qt::NoPen);
        p->setBrush(col);
        p->drawPolygon(head);
    }
    QPointF m_src, m_cursor;
    QRectF m_target;
    bool m_legal = false;
};

namespace {
// Hard cap on a lens's own scan depth (relative to its root). Lenses deepen with
// nesting (baseDepth + level); this bounds the recursion/work no matter how deep
// you stack them — "we are talking C++ here".
constexpr int kMaxLensDepth = 12;
// Fallback size of a base (level-0) root frame, in scene units, used only when no
// view is attached yet. Normally a new base is sized to the viewport (see addBase)
// so fit-to-view fills the window without letterboxing.
constexpr qreal kBaseW = 1100.0, kBaseH = 680.0;

// Walk a directory subtree once, accumulating each dir's subtree weight (≈ treemap
// area — by bytes or file count, matching the active size metric) and its name
// length. Returns this node's subtree weight. The root is excluded from the
// distributions (`isRoot`): it's the panel, not a child cell, and would always be
// the max-weight element, skewing the median/percentile.
double collectDirStats(const core::FsNode &n, bool byBytes, bool isRoot,
                       std::vector<double> &weights, std::vector<int> &nameLens) {
    double w = byBytes ? static_cast<double>(n.sizeBytes) : static_cast<double>(n.fileCount);
    for (const auto &c : n.children)
        w += collectDirStats(*c, byBytes, false, weights, nameLens);
    w = std::max(w, 1.0);
    if (!isRoot) {
        weights.push_back(w);
        nameLens.push_back(static_cast<int>(n.name.size()));
    }
    return w;
}
} // namespace

GraphScene::GraphScene(QObject *parent) : QGraphicsScene(parent) {}

// Out-of-line (fsnode.h is included here) so the unique_ptr<FsNode> projection trees
// can be freed — the header only forward-declares core::FsNode.
GraphScene::~GraphScene() {
    // Delete all items (frames + their interior treemaps) now, while m_projection is
    // still alive: a base frame's interior holds raw pointers into its projected tree,
    // and member destruction would otherwise free m_projection before ~QGraphicsScene
    // deletes those frames — a latent use-after-free if a TreemapItem ever derefs its
    // root at teardown. Synchronous clear() closes that ordering gap.
    clear();
}

std::vector<FrameItem *> GraphScene::baseFrames() const {
    std::vector<FrameItem *> out;
    for (FrameItem *f : m_frames)
        if (f->level() == 0) // level 0 == a base surface (ADR-304)
            out.push_back(f);
    return out;
}

FrameItem *GraphScene::addBase(std::unique_ptr<core::FsNode> tree) {
    if (!tree)
        return nullptr;
    const core::FsNode *render = tree.get();
    // Size the base to the current viewport so fit-to-view fills the window edge to
    // edge: a fixed aspect would letterbox against the real window and shrink cells,
    // which is why a fixed default always felt too small. Fall back to kBaseW/H when
    // no view is attached yet.
    qreal w = kBaseW, h = kBaseH;
    if (!views().isEmpty() && views().first()->viewport()) {
        const QSize vp = views().first()->viewport()->size();
        if (vp.width() > 1 && vp.height() > 1) {
            w = vp.width();
            h = vp.height();
        }
    }
    auto *base = new FrameItem(render, w, h, this);
    base->adoptTree(std::move(tree)); // the base frame is the sole owner of its scan
    base->setLevel(0);                // a root frame: no parent, no callout, removable
    // Cascade new bases down-right so two opened back-to-back don't sit exactly atop
    // each other. Count existing bases to pick the offset.
    const int n = static_cast<int>(baseFrames().size());
    base->setPos(40.0 + n * 48.0, 40.0 + n * 48.0);
    addItem(base);
    m_frames.push_back(base);
    resolveGroups();     // re-resolve rule groups across all bases (multi-root)
    restackFrames();
    rebuildProjection(); // render the projection (identity until moves are staged)
    Q_EMIT surfacesChanged();
    return base;
}

void GraphScene::removeBase(FrameItem *base) {
    closeFrame(base); // generic teardown + cascade; re-resolves groups for a base
}

void GraphScene::clearBases() {
    // Closing every base cascade-closes its lenses; iterate over a snapshot since
    // closeFrame mutates m_frames.
    const std::vector<FrameItem *> bases = baseFrames();
    for (FrameItem *b : bases)
        closeFrame(b);
}

void GraphScene::resolveGroups() {
    // Resolve over each base's immutable scanned source, not its (possibly projected)
    // render root — group identity is stable across staged moves. NOTE: with path
    // keys the overlay only lines up with the projection while the ledger is empty;
    // re-resolving over the projected tree needs ADR-100 durable ids (task #14).
    std::vector<const core::FsNode *> roots;
    for (FrameItem *f : baseFrames())
        roots.push_back(f->sourceRoot());
    if (roots.empty())
        m_groups.clear(); // no surfaces: drop stale groups so the panel matches
    else
        core::resolveRuleGroups(roots, m_groups);
}

void GraphScene::rebuildProjection() {
    const std::vector<FrameItem *> bases = baseFrames();
    const std::vector<core::MoveOp> active = m_ledger.active();
    if (active.empty()) {
        // Identity: render the scanned sources directly (no copy).
        m_projection.clear();
        for (FrameItem *b : bases)
            b->setRenderRoot(b->sourceRoot());
    } else {
        std::vector<const core::FsNode *> sources;
        sources.reserve(bases.size());
        for (FrameItem *b : bases)
            sources.push_back(b->sourceRoot());
        // projectForest builds the new forest from the (still-live) sources before the
        // assignment frees the previous m_projection, so new node addresses can't alias
        // freed ones — keep this order so setRenderRoot's identity guard stays sound.
        m_projection = core::projectForest(sources, active);
        for (std::size_t i = 0; i < bases.size(); ++i)
            bases[i]->setRenderRoot(m_projection[i] ? m_projection[i].get()
                                                    : bases[i]->sourceRoot());
    }
    updateSceneBounds();
    refreshCallouts();
}

void GraphScene::updateGroupOverlay() {
    for (FrameItem *f : m_frames)
        f->update(); // every surface carries the same overlay
}

std::pair<FrameItem *, const core::FsNode *>
GraphScene::surfaceCellAt(const QPointF &scenePos) const {
    // Any surface — base or lens (ADR-302 #13) — can be a drop target. Frames overlap,
    // so pick the topmost (highest z) whose interior holds a cell under the point, which
    // matches what the user sees on top. A lens shows a static deep scan of a base
    // subtree; a drop onto a lens cell that the base scan also contains stages against the
    // base (base + queue reflect it; the lens snapshot itself doesn't re-flow), while a
    // cell deeper than the base scan won't resolve — updateMoveDrag reads it red.
    FrameItem *bestFrame = nullptr;
    const core::FsNode *bestNode = nullptr;
    qreal bestZ = -1e18;
    for (FrameItem *f : m_frames) {
        TreemapItem *t = f->interiorTreemap();
        if (!t)
            continue;
        const QPointF local = t->mapFromScene(scenePos);
        if (!t->boundingRect().contains(local))
            continue;
        if (const core::FsNode *n = t->cellNodeAt(local); n && f->zValue() >= bestZ) {
            bestFrame = f;
            bestNode = n;
            bestZ = f->zValue();
        }
    }
    return {bestFrame, bestNode};
}

namespace {
// Index every node of a render tree by its key, so a dragged/target node — possibly in
// a lens's independent tree — can be mapped to the base node replay will actually touch.
void indexByKey(const core::FsNode *n, QHash<core::MemberKey, const core::FsNode *> &out) {
    if (!n)
        return;
    out.insert(core::keyFor(*n), n); // path/identity aliasing: last wins (ADR-100 / task #14)
    for (const auto &c : n->children)
        indexByKey(c.get(), out);
}
} // namespace

bool GraphScene::beginMoveDrag(const core::FsNode *source, const QPointF &sourceCenterScene) {
    if (!source)
        return false;
    m_dragSource = source;
    m_dragSourceCenter = sourceCenterScene;
    m_dragTarget = nullptr;
    m_dragLegal = false;
    // Snapshot the base projection's keys for the drag. The projection can't change while
    // a drag is in flight (no ledger edits mid-drag), so one build serves every move.
    m_dragKeyIndex.clear();
    for (FrameItem *b : baseFrames())
        indexByKey(b->node(), m_dragKeyIndex);
    if (!m_dragOverlay) {
        m_dragOverlay = new MoveDragOverlay();
        addItem(m_dragOverlay);
    }
    m_dragOverlay->setState(sourceCenterScene, sourceCenterScene, QRectF(), false);
    return true;
}

void GraphScene::updateMoveDrag(const QPointF &cursorScene) {
    if (!m_dragSource || !m_dragOverlay)
        return;
    const auto [frame, node] = surfaceCellAt(cursorScene);
    m_dragTarget = node;
    // Resolve both endpoints to the base node replay will move (a lens cell lives in a
    // separate tree, and a lens cell deeper than the base scan has no base counterpart at
    // all). Checking the resolved base nodes makes the affordance match the result — a
    // drop that won't project reads red and never enters the ledger.
    const core::FsNode *bsrc = m_dragKeyIndex.value(core::keyFor(*m_dragSource), nullptr);
    const core::FsNode *bdst = node ? m_dragKeyIndex.value(core::keyFor(*node), nullptr) : nullptr;
    m_dragLegal = bsrc && bdst && core::checkMove(bsrc, bdst) == core::MoveLegality::Ok;
    QRectF targetScene;
    if (frame && node) {
        const QRectF itemRect = frame->interiorTreemap()->cellRectForNode(node);
        if (!itemRect.isNull())
            targetScene = frame->interiorTreemap()->mapToScene(itemRect).boundingRect();
    }
    m_dragOverlay->setState(m_dragSourceCenter, cursorScene, targetScene, m_dragLegal);
}

void GraphScene::endMoveDrag(bool drop) {
    const bool commit = drop && m_dragLegal && m_dragSource && m_dragTarget;
    core::MoveOp op;
    if (commit)
        op = core::MoveOp{core::keyFor(*m_dragSource), core::keyFor(*m_dragTarget),
                          m_dragSource->name};
    // Tear the overlay + state down now — the render nodes may be freed by the rebuild.
    if (m_dragOverlay) {
        delete m_dragOverlay;
        m_dragOverlay = nullptr;
    }
    m_dragSource = nullptr;
    m_dragTarget = nullptr;
    m_dragLegal = false;
    m_dragKeyIndex.clear();
    if (!commit)
        return;
    m_ledger.append(op);
    // Defer the re-projection: rebuildProjection deletes + recreates the interior
    // treemaps (FrameItem::setRenderRoot), including the one whose mouseReleaseEvent is
    // on the stack right now — freeing the live grabber inline is a use-after-free (the
    // same race the frame-close path defers around).
    QTimer::singleShot(0, this, [this] {
        rebuildProjection();
        Q_EMIT ledgerChanged();   // the queue dock re-lists the staged ops
        Q_EMIT surfacesChanged(); // refresh the dock/status against the new projection
    });
}

void GraphScene::undoMove() {
    if (m_ledger.undo()) {
        rebuildProjection();
        Q_EMIT ledgerChanged();
    }
}

void GraphScene::redoMove() {
    if (m_ledger.redo()) {
        rebuildProjection();
        Q_EMIT ledgerChanged();
    }
}

void GraphScene::clearMoves() {
    if (m_ledger.empty())
        return;
    m_ledger.clear();
    rebuildProjection();
    Q_EMIT ledgerChanged();
}

void GraphScene::scrubTo(int step) {
    if (step == m_ledger.step())
        return;
    m_ledger.setStep(step);
    rebuildProjection();
    Q_EMIT ledgerChanged();
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
    // The callout's origin is the source node in the source surface — the frame the
    // double-click happened in (a base or a lens), tracked dynamically.
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
    // or after clearBases() already removed it. The membership check is a pointer
    // compare (no deref), so it is safe even if `frame` is already deleted.
    const auto it = std::find(m_frames.begin(), m_frames.end(), frame);
    if (it == m_frames.end())
        return;
    const bool wasBase = frame->level() == 0; // read before teardown (ADR-304)
    m_frames.erase(it); // remove before recursing so a re-entrant call can't re-find it

    // Close descendants first (frames opened from within this one), so closing an
    // upstream frame — or a base — never leaves its lenses dangling.
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

    // Removing a base surface drops its tree from the canvas: re-resolve rule groups
    // over the remaining bases (so its groups go) and tell the dock. Cascade-closed
    // lenses are level > 0, so this fires once, for the base itself.
    if (wasBase) {
        resolveGroups();
        updateGroupOverlay();
        rebuildProjection(); // re-project the survivors (ops into the gone base now no-op)
        Q_EMIT surfacesChanged();
    }
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
    for (FrameItem *f : m_frames)
        f->rebuildInterior(); // recreate each interior treemap with the new metric
    refreshCallouts();
}

void GraphScene::setColorRamp(int ramp) {
    m_colorRamp = ramp;
    for (FrameItem *f : m_frames)
        f->rebuildInterior(); // recreate each interior treemap with the new ramp
    refreshCallouts();
}

void GraphScene::setReveal(double factor) {
    m_reveal = factor;
    for (FrameItem *f : m_frames)
        f->setReveal(factor); // live — paint-only, no rebuild
}

void GraphScene::setDetail(double factor) {
    m_detail = factor;
    for (FrameItem *f : m_frames)
        f->setDetail(factor); // live — paint-only, no rebuild
}

void GraphScene::setFileMode(int mode) {
    m_fileMode = mode;
    for (FrameItem *f : m_frames)
        f->setFileMode(mode); // live — paint-only, no rebuild
}

void GraphScene::fitNamesToTypical() {
    if (views().isEmpty())
        return;
    const double z = views().first()->transform().m11(); // device px per scene unit
    if (z <= 0.0)
        return;
    const bool byBytes = m_sizeMetric != 0; // match the active area metric (TreemapItem::Bytes)
    constexpr double kCharPx = 7.0;     // ≈ average char width at the 11px title font
    constexpr double kInsetPx = 8.0;    // title text inset (renderer elides to width − 6)
    constexpr double kPercentile = 0.9; // target a typical name; long outliers still truncate
    constexpr double kMaxScale = 12.0;  // bound a single grow step so the map stays navigable
    for (FrameItem *base : baseFrames()) {
        const core::FsNode *root = base->sourceRoot();
        if (!root)
            continue;
        std::vector<double> weights;
        std::vector<int> nameLens;
        const double rootW = collectDirStats(*root, byBytes, true, weights, nameLens);
        if (weights.empty() || rootW <= 0.0)
            continue;
        std::sort(weights.begin(), weights.end());
        std::sort(nameLens.begin(), nameLens.end());
        const double medianW = weights[weights.size() / 2];
        const int pLen = nameLens[static_cast<std::size_t>(kPercentile * (nameLens.size() - 1))];
        // The median cell occupies ≈ medianW/rootW of the map's area, so its *current*
        // on-screen width is ≈ sqrt(fraction) × the base's current scene width × the
        // view scale. Grow only the shortfall to the typical name's pixel width — using
        // the actual current size (not the viewport) makes repeat clicks converge
        // rather than multiply.
        const double fraction = medianW / rootW;
        const double cellDeviceW = std::sqrt(fraction) * base->panelSize().width() * z;
        const double targetPx = pLen * kCharPx + kInsetPx;
        const double s = std::clamp(targetPx / std::max(1.0, cellDeviceW), 1.0, kMaxScale);
        const QSizeF cur = base->panelSize();
        base->resizePanel(cur.width() * s, cur.height() * s); // re-squarifies; no re-fit
    }
    updateSceneBounds();
    refreshCallouts();
}

void GraphScene::updateSceneBounds() {
    // Generous panning room so exploring (dragging frames far, several bases side by
    // side) never butts up against the canvas edge: a large floor plus a margin that
    // scales with the content. Bounded by a hard maximum so the scroll range stays
    // finite (ScrollHandDrag is constrained to sceneRect).
    constexpr qreal kFloor = 4000.0;     // minimum margin beyond the content, each side
    constexpr qreal kMaxExtent = 80000.0; // hard cap on either scene dimension
    const QRectF b = itemsBoundingRect();
    const qreal m = std::max({kFloor, b.width(), b.height()});
    QRectF r = b.adjusted(-m, -m, m, m);
    // Clamp to the maximum extent, kept centred on the content.
    if (r.width() > kMaxExtent || r.height() > kMaxExtent) {
        const QPointF c = b.center();
        const qreal halfW = std::min(r.width(), kMaxExtent) / 2.0;
        const qreal halfH = std::min(r.height(), kMaxExtent) / 2.0;
        r = QRectF(c.x() - halfW, c.y() - halfH, halfW * 2.0, halfH * 2.0);
    }
    setSceneRect(r);
}

} // namespace ui
