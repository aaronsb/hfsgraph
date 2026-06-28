#include "frameitem.h"

#include "core/fsnode.h"
#include "graphscene.h"
#include "treemapitem.h"

#include <algorithm>

#include <QApplication>
#include <QBrush>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QTimer>
#include <QTransform>
#include <QVector>

namespace ui {

namespace {
constexpr qreal kHeader = 22.0; // title-strip height (item units)
constexpr qreal kPad = 3.0;     // inset between panel edge and interior
constexpr qreal kShadow = 18.0; // dither drop-shadow offset (deep, retro look)
constexpr qreal kCloseW = 22.0; // close-box width at the header's right

// An ordered-dither (Bayer 4×4, ~50%) tile of `ink`, cached per colour. The
// project's future-retro texture: dark for drop shadows, light for the callout
// "zoom-from" frustum. Used in device space so it stays pixel-perfect at any zoom.
QBrush ditherBrush(const QColor &ink) {
    static QHash<QRgb, QBrush> cache;
    const QRgb key = ink.rgba();
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    static const int bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    QImage img(4, 4, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            if (bayer[y][x] < 8) // half the cells set → 50% coverage
                img.setPixelColor(x, y, ink);
    QBrush brush(QPixmap::fromImage(img));
    cache.insert(key, brush);
    return brush;
}

// Convex hull (monotone chain) of a small point set — used to wrap the origin
// square and the frame into one "zoom-from" polygon that auto-picks the nearest
// corners.
QPolygonF convexHull(QVector<QPointF> pts) {
    std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    const int n = pts.size();
    if (n < 3)
        return QPolygonF(pts);
    auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    QVector<QPointF> hull(2 * n);
    int k = 0;
    for (int i = 0; i < n; ++i) { // lower hull
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0)
            --k;
        hull[k++] = pts[i];
    }
    for (int i = n - 2, lower = k + 1; i >= 0; --i) { // upper hull
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0)
            --k;
        hull[k++] = pts[i];
    }
    hull.resize(k - 1);
    return QPolygonF(hull);
}
} // namespace

// ---- CalloutItem --------------------------------------------------------------

CalloutItem::CalloutItem(const core::FsNode *originNode, FrameItem *sourceFrame, FrameItem *frame)
    : m_originNode(originNode), m_sourceFrame(sourceFrame), m_frame(frame) {
    setZValue(0);                          // set properly by the scene (below its frame)
    setAcceptedMouseButtons(Qt::NoButton); // a passive overlay — never intercept clicks
}

void CalloutItem::recomputeOrigin() {
    // The origin square lives in the source surface — the frame the double-click
    // happened in (a base or a lens), since every surface is now a FrameItem
    // (ADR-304). Recompute its scene rect from the *current* layout so the callout
    // re-anchors after the source frame is moved or resized.
    if (!m_sourceFrame || !m_originNode)
        return;
    TreemapItem *map = m_sourceFrame->interiorTreemap();
    if (!map)
        return;
    const QRectF r = map->cellRectForNode(m_originNode);
    if (!r.isNull())
        m_origin = map->mapToScene(r).boundingRect();
}

void CalloutItem::refresh() {
    prepareGeometryChange();
    recomputeOrigin();
    update();
}

void CalloutItem::setSource(const core::FsNode *originNode, FrameItem *sourceFrame) {
    m_originNode = originNode;
    m_sourceFrame = sourceFrame;
    refresh();
}

QRectF CalloutItem::boundingRect() const {
    // Item sits at scene origin (no transform), so local == scene coordinates.
    QRectF frameRect(m_frame->pos(), m_frame->panelSize());
    return m_origin.united(frameRect).adjusted(-3, -3, 3, 3); // margin for the AA stroke
}

void CalloutItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    const auto mode = scene()
                          ? static_cast<CalloutMode>(static_cast<GraphScene *>(scene())->calloutMode())
                          : CalloutMode::Filled;
    if (mode == CalloutMode::Off)
        return;

    const QRectF frameRect(m_frame->pos(), m_frame->panelSize());

    if (mode == CalloutMode::Lines) {
        // Two diagonal hairlines (no fill): origin UR → frame UR, origin LL → frame LL.
        QPen pen(QColor(165, 180, 210, 200));
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setRenderHint(QPainter::Antialiasing, true);
        p->drawLine(m_origin.topRight(), QPointF(frameRect.right(), frameRect.top()));
        p->drawLine(m_origin.bottomLeft(), QPointF(frameRect.left(), frameRect.bottom()));
        return;
    }

    // Filled "zoom-from" frustum: convex hull of the origin square and the frame, with
    // both rectangles subtracted out (vertex occlusion) so only the connecting cone is
    // filled — not the boxes. Reads better than crossing lines when several are open.
    const QVector<QPointF> pts = {
        m_origin.topLeft(),  m_origin.topRight(),  m_origin.bottomRight(),  m_origin.bottomLeft(),
        frameRect.topLeft(), frameRect.topRight(), frameRect.bottomRight(), frameRect.bottomLeft()};
    QPainterPath hull;
    hull.addPolygon(convexHull(pts));
    hull.closeSubpath();
    QPainterPath cut;
    cut.addRect(m_origin);
    cut.addRect(frameRect);
    const QPainterPath cone = hull.subtracted(cut);

    // Device-space light dither: surface area scales with the frames, the tile stays
    // pixel-perfect (same language as the drop shadow, lighter colour).
    const QTransform tf = p->worldTransform();
    p->setWorldMatrixEnabled(false);
    p->fillPath(tf.map(cone), ditherBrush(QColor(206, 210, 222, 120)));
    p->setWorldMatrixEnabled(true);
}

// ---- FrameItem ----------------------------------------------------------------

FrameItem::FrameItem(const core::FsNode *node, qreal width, qreal height, GraphScene *scene)
    : m_node(node), m_w(width), m_h(height), m_scene(scene) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemClipsChildrenToShape, true); // keep the interior treemap inside the panel
    setFiltersChildEvents(true);             // see sceneEventFilter (click-to-raise)

    rebuildInterior(); // builds m_interior from the scene's current metric/ramp/LOD

    m_grip = new ResizeGrip(this); // child; anchors to the bottom-right corner
    m_grip->setPos(m_w, m_h);
}

void FrameItem::rebuildInterior() {
    delete m_interior; // child item, removed from the scene on delete (null on first build)
    const QRectF in = interiorRect();
    m_interior = new TreemapItem(m_node, in.width(), in.height(),
                                 static_cast<TreemapItem::SizeMetric>(m_scene->sizeMetric()),
                                 static_cast<TreemapItem::Ramp>(m_scene->colorRamp()), m_scene);
    m_interior->setLod(m_scene->lod());
    m_interior->setGroupStore(&m_scene->groups());
    m_interior->setOwnerFrame(this); // so its double-clicks record this frame as the parent
    m_interior->setParentItem(this);
    m_interior->setPos(in.topLeft());
}

// Out-of-line so unique_ptr<core::FsNode> sees the complete type here (fsnode.h is
// included) — this is where the frame's own deep-scanned subtree is reclaimed.
FrameItem::~FrameItem() {
    // Destroy the interior treemap *before* m_ownTree is freed: the interior holds
    // raw pointers into that tree, and otherwise the base ~QGraphicsObject would
    // delete it only after this object's members (incl. m_ownTree) are gone — a
    // latent use-after-free in the owned-tree model.
    delete m_interior;
    m_interior = nullptr;
}

void FrameItem::adoptTree(std::unique_ptr<core::FsNode> tree) {
    m_ownTree = std::move(tree); // sole owner; freed when this frame is destroyed
}

void FrameItem::resizePanel(qreal width, qreal height) {
    constexpr qreal kMinW = 200.0, kMinH = 140.0;
    const qreal w = std::max(kMinW, width), h = std::max(kMinH, height);
    if (qFuzzyCompare(w, m_w) && qFuzzyCompare(h, m_h))
        return;
    prepareGeometryChange();
    m_w = w;
    m_h = h;
    const QRectF in = interiorRect();
    if (m_interior)
        m_interior->setSize(in.width(), in.height()); // re-squarify → more text room
    if (m_grip)
        m_grip->setPos(m_w, m_h);
    if (m_scene)
        m_scene->refreshCalloutsFor(this); // this frame's callout + child lenses sourced here
    update();
}

void FrameItem::setRenderRoot(const core::FsNode *root) {
    if (root == m_node)
        return; // already rendering this root — skip the interior rebuild
    m_node = root;
    rebuildInterior();
    update(); // the header title tracks m_node
}

void FrameItem::setLod(qreal factor) {
    if (m_interior)
        m_interior->setLod(factor);
}

QSizeF FrameItem::panelSize() const { return QSizeF(m_w, m_h); }

QRectF FrameItem::panelRect() const { return QRectF(0, 0, m_w, m_h); }
QRectF FrameItem::headerRect() const { return QRectF(0, 0, m_w, kHeader); }
QRectF FrameItem::closeRect() const {
    // The × is drawn in device space at a constant 22px; size the hit-rect to match
    // (item units = device / zoom) so the clickable area lines up with the glyph
    // instead of extending left of it at zoom > 1.
    const qreal w = kCloseW / (m_lastZoom > 0 ? m_lastZoom : 1.0);
    return QRectF(m_w - w, 0, w, kHeader);
}
QRectF FrameItem::interiorRect() const {
    return QRectF(kPad, kHeader, m_w - 2 * kPad, m_h - kHeader - kPad);
}

QRectF FrameItem::boundingRect() const {
    return QRectF(0, 0, m_w + kShadow, m_h + kShadow); // room for the shadow
}

void FrameItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, false);
    const bool dark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
    const QTransform tf = p->worldTransform();
    const qreal zoom = tf.m11();
    m_lastZoom = zoom; // so closeRect()'s hit area matches the device-drawn ×

    // Ordered-dither drop shadow. The shadow's surface AREA scales with the frame
    // (so it reads as getting closer on zoom-in), but the dither tile stays
    // pixel-perfect — painted in device space, so the 4×4 pattern tiles *more*
    // rather than stretching.
    {
        const QRectF devShadow = tf.mapRect(QRectF(kShadow, kShadow, m_w, m_h));
        p->setWorldMatrixEnabled(false);
        p->fillRect(devShadow, ditherBrush(QColor(0, 0, 0, 150)));
        p->setWorldMatrixEnabled(true);
    }

    // Panel body + header band (the interior treemap paints itself as a child).
    const QColor body = dark ? QColor(38, 40, 46) : QColor(238, 238, 240);
    const QColor head = dark ? QColor(70, 78, 96) : QColor(120, 134, 160);
    p->fillRect(panelRect(), body);
    p->fillRect(headerRect(), head);

    // Header chrome — title + × — drawn in device space at a CONSTANT screen size,
    // exactly like the base canvas labels (vertically centred in the header band).
    const QPointF devTL = tf.map(QPointF(0, 0));
    const qreal devW = m_w * zoom;
    const qreal devHdr = kHeader * zoom;
    constexpr qreal closeDevW = 22.0;
    p->setWorldMatrixEnabled(false);
    QFont f = p->font();
    f.setPixelSize(12);
    f.setBold(true);
    p->setFont(f);
    p->setPen(head.lightness() < 140 ? QColor(238, 238, 238) : QColor(18, 18, 18));
    const QRectF devTitle(devTL.x() + 6, devTL.y(), devW - 6 - closeDevW, devHdr);
    p->drawText(devTitle, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(f).elidedText(m_node->name, Qt::ElideMiddle,
                                           static_cast<int>(std::max<qreal>(8.0, devTitle.width()))));
    p->drawText(QRectF(devTL.x() + devW - closeDevW, devTL.y(), closeDevW, devHdr),
                Qt::AlignCenter, QStringLiteral("×"));
    p->setWorldMatrixEnabled(true);

    // Panel border.
    QPen border(dark ? QColor(0, 0, 0, 200) : QColor(0, 0, 0, 140));
    border.setCosmetic(true);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRect(panelRect());
}

void FrameItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    const QPointF pos = event->pos();
    if (closeRect().contains(pos)) {
        // Arm the close, but do it on RELEASE — not here. Removing/deleting this item
        // while it is the mouse grabber (which it becomes right after this handler)
        // leaves the scene delivering the release to a dangling item (intermittent
        // segfault). Acting on release means the grab is released first.
        m_pendingClose = true;
        event->accept();
        return;
    }
    if (headerRect().contains(pos)) {
        m_dragging = true;
        m_dragOffset = event->scenePos() - this->pos();
        if (m_scene)
            m_scene->raiseFrame(this);
        event->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(event); // interior treemap handles the rest
}

void FrameItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (m_dragging) {
        setPos(event->scenePos() - m_dragOffset);
        if (m_scene)
            m_scene->refreshCalloutsFor(this); // this frame + child lenses sourced here
        event->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

// ---- ResizeGrip ---------------------------------------------------------------

ResizeGrip::ResizeGrip(FrameItem *frame) : m_frame(frame) {
    setParentItem(frame);
    setFlag(ItemIgnoresTransformations, true); // constant screen size at the corner
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeFDiagCursor);
    setZValue(10); // above the interior treemap so the corner is always grabbable
}

QRectF ResizeGrip::boundingRect() const {
    return QRectF(-16, -16, 16, 16); // device px (ignores view transform), up-left of the corner
}

void ResizeGrip::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    QPen pen(QColor(210, 210, 220, 210), 1.5);
    p->setPen(pen);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->drawLine(QPointF(-12, -2), QPointF(-2, -12)); // three-rung corner grip glyph
    p->drawLine(QPointF(-9, -2), QPointF(-2, -9));
    p->drawLine(QPointF(-6, -2), QPointF(-2, -6));
}

void ResizeGrip::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    m_active = true;
    m_startScene = event->scenePos();
    m_startSize = m_frame->panelSize();
    event->accept();
}

void ResizeGrip::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (!m_active) {
        QGraphicsItem::mouseMoveEvent(event);
        return;
    }
    const QPointF d = event->scenePos() - m_startScene; // scene units → intuitive at any zoom
    m_frame->resizePanel(m_startSize.width() + d.x(), m_startSize.height() + d.y());
    event->accept();
}

void ResizeGrip::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    m_active = false;
    event->accept();
}

bool FrameItem::sceneEventFilter(QGraphicsItem *, QEvent *event) {
    if (event->type() == QEvent::GraphicsSceneMousePress && m_scene)
        m_scene->raiseFrame(this); // bring forward, then let the child handle it
    return false;                  // never swallow — interior select/double-click stays live
}

void FrameItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    if (m_pendingClose) {
        m_pendingClose = false;
        event->accept();
        // Only close if the release is still on the ×, like a normal button. Defer to
        // the next tick so the scene finishes releasing the grab before we delete.
        if (m_scene && closeRect().contains(event->pos())) {
            GraphScene *scene = m_scene;
            QPointer<FrameItem> self = this; // nulls out if destroyed before the tick
            QTimer::singleShot(0, scene, [scene, self] {
                if (self)
                    scene->closeFrame(self);
            });
        }
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
}

} // namespace ui
