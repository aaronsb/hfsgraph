#include "frameitem.h"

#include "core/fsnode.h"
#include "graphscene.h"
#include "treemapitem.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

namespace ui {

namespace {
constexpr qreal kHeader = 22.0; // title-strip height (item units)
constexpr qreal kPad = 3.0;     // inset between panel edge and interior
constexpr qreal kShadow = 9.0;  // dither drop-shadow offset
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

FrameItem::FrameItem(const core::FsNode *node, qreal width, qreal height, GraphScene *scene)
    : m_node(node), m_w(width), m_h(height), m_scene(scene) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemClipsChildrenToShape, true); // keep the interior treemap inside the panel

    const QRectF in = interiorRect();
    m_interior = new TreemapItem(node, in.width(), in.height(),
                                 static_cast<TreemapItem::SizeMetric>(m_scene->sizeMetric()),
                                 static_cast<TreemapItem::Ramp>(m_scene->colorRamp()), m_scene);
    m_interior->setLod(m_scene->lod());
    m_interior->setGroupStore(&m_scene->groups());
    m_interior->setParentItem(this);
    m_interior->setPos(in.topLeft());
}

void FrameItem::setLod(qreal factor) {
    if (m_interior)
        m_interior->setLod(factor);
}

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

    // Ordered-dither drop shadow, offset down-right behind the panel.
    p->fillRect(QRectF(kShadow, kShadow, m_w, m_h), ditherBrush());

    // Panel body + header strip (the interior treemap paints itself as a child).
    const QColor body = dark ? QColor(38, 40, 46) : QColor(238, 238, 240);
    const QColor head = dark ? QColor(70, 78, 96) : QColor(120, 134, 160);
    p->fillRect(panelRect(), body);
    p->fillRect(headerRect(), head);

    // Title (elided) + a × close affordance.
    p->setPen(head.lightness() < 140 ? QColor(238, 238, 238) : QColor(18, 18, 18));
    QFont f = p->font();
    f.setPixelSize(12);
    f.setBold(true);
    p->setFont(f);
    const QRectF tr = headerRect().adjusted(6, 0, -kCloseW, 0);
    p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(f).elidedText(m_node->name, Qt::ElideMiddle,
                                           static_cast<int>(tr.width())));
    p->drawText(closeRect(), Qt::AlignCenter, QStringLiteral("×"));

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
        event->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
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
