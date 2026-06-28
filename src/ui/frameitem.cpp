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
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QTransform>

namespace ui {

namespace {
constexpr qreal kHeader = 22.0; // title-strip height (item units)
constexpr qreal kPad = 3.0;     // inset between panel edge and interior
constexpr qreal kShadow = 18.0; // dither drop-shadow offset (deep, retro look)
constexpr qreal kCloseW = 22.0; // close-box width at the header's right

// An ordered-dither (Bayer 4×4, ~50%) tile of translucent black — the project's
// future-retro shadow texture. Built once and reused as a brush.
QBrush ditherBrush() {
    static QBrush brush = [] {
        static const int bayer[4][4] = {
            {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        QImage img(4, 4, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        const QColor ink(0, 0, 0, 150);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                if (bayer[y][x] < 8) // half the cells set → 50% coverage
                    img.setPixelColor(x, y, ink);
        return QBrush(QPixmap::fromImage(img));
    }();
    return brush;
}
} // namespace

// ---- CalloutItem --------------------------------------------------------------

CalloutItem::CalloutItem(const QRectF &originSceneRect, FrameItem *frame)
    : m_origin(originSceneRect), m_frame(frame) {
    setZValue(0);                              // set properly by the scene (below its frame)
    setAcceptedMouseButtons(Qt::NoButton);     // a passive overlay — never intercept clicks
}

void CalloutItem::refresh() {
    prepareGeometryChange();
    update();
}

QRectF CalloutItem::boundingRect() const {
    // Item sits at scene origin (no transform), so local == scene coordinates.
    QRectF frameRect(m_frame->pos(), m_frame->panelSize());
    return m_origin.united(frameRect).adjusted(-3, -3, 3, 3); // margin for the AA stroke
}

void CalloutItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    const QPointF fpos = m_frame->pos();
    const QSizeF fs = m_frame->panelSize();
    const QPointF frameUR(fpos.x() + fs.width(), fpos.y());
    const QPointF frameLL(fpos.x(), fpos.y() + fs.height());

    QPen pen(QColor(150, 170, 210, 200));
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setRenderHint(QPainter::Antialiasing, true); // diagonal hairlines read better smoothed
    p->drawLine(m_origin.topRight(), frameUR);
    p->drawLine(m_origin.bottomLeft(), frameLL);
}

// ---- FrameItem ----------------------------------------------------------------

FrameItem::FrameItem(const core::FsNode *node, qreal width, qreal height, GraphScene *scene)
    : m_node(node), m_w(width), m_h(height), m_scene(scene) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemClipsChildrenToShape, true); // keep the interior treemap inside the panel
    setFiltersChildEvents(true);             // see sceneEventFilter (click-to-raise)

    const QRectF in = interiorRect();
    m_interior = new TreemapItem(node, in.width(), in.height(),
                                 static_cast<TreemapItem::SizeMetric>(m_scene->sizeMetric()),
                                 static_cast<TreemapItem::Ramp>(m_scene->colorRamp()), m_scene);
    m_interior->setLod(m_scene->lod());
    m_interior->setGroupStore(&m_scene->groups());
    m_interior->setOwnerFrame(this); // so its double-clicks record this as the parent
    m_interior->setParentItem(this);
    m_interior->setPos(in.topLeft());

    m_grip = new ResizeGrip(this); // child; anchors to the bottom-right corner
    m_grip->setPos(m_w, m_h);
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
    if (m_callout)
        m_callout->refresh();
    update();
}

void FrameItem::setLod(qreal factor) {
    if (m_interior)
        m_interior->setLod(factor);
}

QSizeF FrameItem::panelSize() const { return QSizeF(m_w, m_h); }

QRectF FrameItem::panelRect() const { return QRectF(0, 0, m_w, m_h); }
QRectF FrameItem::headerRect() const { return QRectF(0, 0, m_w, kHeader); }
QRectF FrameItem::closeRect() const { return QRectF(m_w - kCloseW, 0, kCloseW, kHeader); }
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

    // Ordered-dither drop shadow. The shadow's surface AREA scales with the frame
    // (so it reads as getting closer on zoom-in), but the dither tile stays
    // pixel-perfect — painted in device space, so the 4×4 pattern tiles *more*
    // rather than stretching.
    {
        const QRectF devShadow = tf.mapRect(QRectF(kShadow, kShadow, m_w, m_h));
        p->setWorldMatrixEnabled(false);
        p->fillRect(devShadow, ditherBrush());
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
        event->accept(); // consume before deletion, else it falls through to the base map
        if (m_scene)
            m_scene->closeFrame(this); // deletes this item
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
        if (m_callout)
            m_callout->refresh(); // the callout anchors to the frame's new position
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
    QGraphicsItem::mouseReleaseEvent(event);
}

} // namespace ui
