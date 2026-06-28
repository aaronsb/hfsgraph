#include "nodeitem.h"

#include "core/fsnode.h"
#include "graphscene.h"

#include <algorithm>
#include <cmath>

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QFileSystemModel>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneMouseEvent>
#include <QHeaderView>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QTreeView>

namespace ui {

namespace {
constexpr qreal kPad = 6.0;

void styleView(QAbstractItemView *v) {
    v->setFrameShape(QFrame::NoFrame);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setFocusPolicy(Qt::NoFocus);
}
} // namespace

QString NodeItem::humanSize(qint64 bytes) {
    const char *unit[] = {"B", "KB", "MB", "GB", "TB"};
    double s = static_cast<double>(bytes);
    int i = 0;
    while (s >= 1024.0 && i < 4) {
        s /= 1024.0;
        ++i;
    }
    return QString::number(s, 'f', i == 0 ? 0 : 1) + QStringLiteral(" ") +
           QString::fromLatin1(unit[i]);
}

NodeItem::NodeItem(const core::FsNode *node, bool hasChildren, bool collapsed,
                   std::function<void(const core::FsNode *)> onToggle, GraphScene *scene)
    : m_node(node), m_hasChildren(hasChildren), m_collapsed(collapsed),
      m_onToggle(std::move(onToggle)), m_scene(scene), m_width(DefaultWidth),
      m_openListH(DefaultListH) {
    m_hasFiles = m_node->fileCount > 0;

    const int dirs = static_cast<int>(m_node->children.size());
    m_stats1 = QStringLiteral("%1 file%2 · %3 dir%4")
                   .arg(m_node->fileCount)
                   .arg(m_node->fileCount == 1 ? "" : "s")
                   .arg(dirs)
                   .arg(dirs == 1 ? "" : "s");
    if (m_hasFiles)
        m_stats2 = humanSize(m_node->sizeBytes) + QStringLiteral(" on disk");

    recomputeHeight();

    setFlag(ItemIsMovable, true); // drag-to-arrange (layout only — ADR-300)
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setCursor(Qt::ArrowCursor);
}

void NodeItem::buildViewer() {
    if (m_proxy || !m_hasFiles)
        return;
    // One model, two views (icon grid / detail list) — like a file dialog.
    // DontWatchForChanges avoids spawning an inotify watcher per node.
    m_stack = new QStackedWidget();
    m_fsModel = new QFileSystemModel(m_stack);
    m_fsModel->setOption(QFileSystemModel::DontWatchForChanges, true);
    m_fsModel->setFilter(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    // The model loads its directory on a background thread; a fit requested before
    // the rows arrive is finished here, once they have. Context is m_fsModel so the
    // connection drops when the model (and this item) is destroyed.
    QObject::connect(m_fsModel, &QFileSystemModel::directoryLoaded, m_fsModel,
                     [this](const QString &) {
                         if (m_pendingListFit)
                             applyListFit();
                     });
    const QModelIndex root = m_fsModel->setRootPath(m_node->path);

    auto *iconView = new QListView();
    iconView->setModel(m_fsModel);
    iconView->setRootIndex(root);
    iconView->setViewMode(QListView::IconMode); // array of icons/names
    iconView->setFlow(QListView::LeftToRight);
    iconView->setWrapping(true);                // wrap into a grid
    iconView->setResizeMode(QListView::Adjust); // reflow on resize
    iconView->setMovement(QListView::Static);
    iconView->setUniformItemSizes(true);
    iconView->setWordWrap(true);
    iconView->setIconSize(QSize(30, 30));
    iconView->setGridSize(QSize(80, 62));
    iconView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // wrap, don't scroll sideways
    styleView(iconView);

    auto *detailView = new QTreeView();
    detailView->setModel(m_fsModel);
    detailView->setRootIndex(root);
    detailView->setRootIsDecorated(false);
    detailView->setItemsExpandable(false);
    detailView->setAlternatingRowColors(true);
    detailView->setIndentation(0);
    detailView->header()->setStretchLastSection(true);
    detailView->setColumnWidth(0, 130);
    styleView(detailView);

    m_stack->addWidget(iconView);   // index 0
    m_stack->addWidget(detailView); // index 1
    m_stack->setCurrentIndex(m_viewMode);

    m_proxy = new QGraphicsProxyWidget(this);
    m_proxy->setWidget(m_stack);
    updateListGeometry();
}

void NodeItem::recomputeHeight() {
    if (!m_shaded && m_hasFiles) {
        m_height = HeaderH + kPad + m_openListH + kPad;
    } else {
        const int lines = m_stats2.isEmpty() ? 1 : 2;
        m_height = HeaderH + lines * LineH + kPad;
    }
}

void NodeItem::setShaded(bool shaded) {
    if (!m_hasFiles)
        return; // nothing to roll down
    m_shaded = shaded;
    if (!m_shaded)
        buildViewer();
    if (m_proxy)
        m_proxy->setVisible(!m_shaded);
    prepareGeometryChange();
    recomputeHeight();
    updateListGeometry();
    update();
    if (m_scene)
        m_scene->onNodeMoved();
}

void NodeItem::toggleShade() {
    setShaded(!m_shaded);
}

void NodeItem::setViewMode(int mode) {
    if (!m_hasFiles)
        return;
    m_viewMode = mode ? 1 : 0;
    if (m_stack)
        m_stack->setCurrentIndex(m_viewMode);
    update();
}

// Size the node so its file viewer shows all entries — node size becomes an
// indicator of object count (icon grid → larger rectangle; list → long column).
void NodeItem::fitToContent() {
    if (!m_hasFiles)
        return;
    setShaded(false); // ensure the viewer exists and is open
    const int count = static_cast<int>(m_node->files.size());
    prepareGeometryChange();
    if (m_viewMode == 0) { // icon grid: roughly square, sized to show every cell
        auto *iconView = qobject_cast<QListView *>(m_stack->widget(0));
        const QSize cell = iconView ? iconView->gridSize() : QSize(80, 62);
        const int sb =
            iconView ? iconView->style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 16;
        constexpr qreal kComfort = 8.0; // beats QListView's internal cell margin
        const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(double(count)))));
        // Aim for a roughly square grid, plus a scrollbar-width gutter so the layout
        // never has to choose between a column and the scrollbar.
        m_width =
            std::clamp<qreal>(cols * cell.width() + 2 * kPad + sb + kComfort, MinWidth, 900.0);
        // Derive rows from the columns that PESSIMISTICALLY fit the (possibly clamped)
        // width — subtract the scrollbar + comfort so this is never more than the view
        // actually packs. Undercounting columns only over-estimates rows, which sizes
        // the height tall enough that no vertical scrollbar appears (a scrollbar would
        // steal width, drop a column, add a row, and bring itself back — the old bug).
        const int fitCols =
            std::max(1, static_cast<int>((m_width - 2 * kPad - sb - kComfort) / cell.width()));
        const int rows = (count + fitCols - 1) / fitCols;
        m_openListH = std::clamp<qreal>(rows * cell.height() + kPad + 6.0, MinListH, 1000.0);
    } else { // detail list: size to the QTreeView's real columns + rows
        auto *tree = qobject_cast<QTreeView *>(m_stack->widget(1));
        if (tree && m_fsModel->rowCount(tree->rootIndex()) > 0) {
            applyListFit(); // model already populated — measure now
            return;         // applyListFit() finishes the geometry update
        }
        // Model loads asynchronously; defer sizing until its rows arrive (see
        // the directoryLoaded hook wired in buildViewer). Give a provisional size
        // so the node isn't tiny in the meantime.
        m_pendingListFit = true;
        m_width = 360.0;
        m_openListH = std::clamp<qreal>(count * 24.0 + 32.0, MinListH, 1200.0);
    }
    recomputeHeight();
    updateListGeometry();
    update();
    if (m_scene)
        m_scene->onNodeMoved();
}

// Size the open detail list to its populated QTreeView: width = sum of the four
// content-fit column widths, height = header row + one row per file. Called once
// the QFileSystemModel has loaded (rows must exist for the measurements to be
// real — see fitToContent() and the directoryLoaded hook).
void NodeItem::applyListFit() {
    m_pendingListFit = false;
    if (m_shaded || m_viewMode != 1 || !m_proxy)
        return;
    auto *tree = qobject_cast<QTreeView *>(m_stack->widget(1));
    if (!tree)
        return;
    const int count = static_cast<int>(m_node->files.size());
    prepareGeometryChange();

    QHeaderView *hdr = tree->header();
    hdr->setStretchLastSection(false);
    // Width = the four content-fit columns + a scrollbar-width gutter so that even
    // if a vertical scrollbar does appear it never squeezes out a horizontal one.
    const int sbExtent = tree->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    qreal w = 2 * kPad + sbExtent + 4.0;
    for (int c = 0; c < m_fsModel->columnCount(); ++c) {
        tree->resizeColumnToContents(c);
        w += tree->columnWidth(c) + 4.0; // a hair of per-column slack
    }
    hdr->setStretchLastSection(true); // let the last column absorb the slack
    m_width = std::clamp<qreal>(w, MinWidth, 900.0);

    // Height = header + one row per file. sizeHintForRow can under-report by the
    // 1px grid line per row, so budget rowH+1 plus a small base slack — sizing to
    // fit every row keeps the vertical scrollbar away.
    const int rowH = std::max(tree->sizeHintForRow(0), 16);
    const int headerH = std::max(hdr->height(), hdr->sizeHint().height());
    m_openListH = std::clamp<qreal>(headerH + count * (rowH + 1) + 8.0, MinListH, 1600.0);

    recomputeHeight();
    updateListGeometry();
    update();
    if (m_scene)
        m_scene->onNodeMoved();
}

void NodeItem::updateListGeometry() {
    if (!m_proxy || m_shaded)
        return;
    const int w = static_cast<int>(m_width - 2 * kPad);
    const int h = static_cast<int>(m_openListH);
    m_stack->setFixedSize(w, h);
    m_proxy->setPos(kPad, HeaderH + kPad);
}

QRectF NodeItem::boundingRect() const {
    return QRectF(0, 0, m_width, m_height);
}

// Header buttons fill slots from the right: collapse (if any children), then
// shade (if any files), then view-mode (only when open).
QRectF NodeItem::slotRect(int fromRight) const {
    return QRectF(m_width - 26.0 - fromRight * SlotStep, 8.0, 16.0, 16.0);
}

QRectF NodeItem::collapseToggleRect() const {
    return m_hasChildren ? slotRect(0) : QRectF();
}

QRectF NodeItem::shadeToggleRect() const {
    return m_hasFiles ? slotRect(m_hasChildren ? 1 : 0) : QRectF();
}

QRectF NodeItem::viewToggleRect() const {
    if (m_shaded || !m_hasFiles)
        return QRectF();
    return slotRect((m_hasChildren ? 1 : 0) + 1);
}

QRectF NodeItem::resizeHandleRect() const {
    return QRectF(m_width - 16.0, m_height - 16.0, 16.0, 16.0);
}

qreal NodeItem::titleRight() const {
    qreal left = m_width - 8.0;
    for (const QRectF &r : {viewToggleRect(), shadeToggleRect(), collapseToggleRect()}) {
        if (!r.isNull())
            left = std::min(left, r.left() - 6.0);
    }
    return left;
}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    const QPalette pal = qApp->palette();
    const QColor fill = pal.color(QPalette::Base);
    const QColor header = pal.color(QPalette::Highlight);
    const QColor headerText = pal.color(QPalette::HighlightedText);
    const QColor text = pal.color(QPalette::Text);
    QColor border = pal.color(QPalette::Mid);
    if (isSelected())
        border = pal.color(QPalette::Highlight);

    const QRectF r = boundingRect();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Body (rounded-rect card — never a circle)
    QPainterPath body;
    body.addRoundedRect(r, 8, 8);
    painter->fillPath(body, fill);

    // Header band: clip to the card silhouette and fill a plain rectangle so the top
    // corners round with the body, the bottom edge stays straight, and the fill covers
    // the full width behind the title.
    painter->save();
    painter->setClipPath(body);
    painter->fillRect(QRectF(0, 0, m_width, HeaderH), header);
    painter->restore();

    // Title (elided)
    painter->setPen(headerText);
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    painter->setFont(titleFont);
    const qreal tr = titleRight();
    const QString title = painter->fontMetrics().elidedText(m_node->name, Qt::ElideMiddle,
                                                            static_cast<int>(tr - 8.0));
    painter->drawText(QRectF(8, 0, tr - 8, HeaderH), Qt::AlignVCenter, title);

    // --- header buttons ---
    painter->setPen(QPen(headerText, 1.6));

    // window-shade: chevron down (open it) when shaded, up (roll up) when open
    if (m_hasFiles) {
        const QRectF s = shadeToggleRect();
        const QPointF c = s.center();
        if (m_shaded) {
            painter->drawLine(QPointF(c.x() - 4, c.y() - 2), QPointF(c.x(), c.y() + 2));
            painter->drawLine(QPointF(c.x() + 4, c.y() - 2), QPointF(c.x(), c.y() + 2));
        } else {
            painter->drawLine(QPointF(c.x() - 4, c.y() + 2), QPointF(c.x(), c.y() - 2));
            painter->drawLine(QPointF(c.x() + 4, c.y() + 2), QPointF(c.x(), c.y() - 2));
        }
    }

    // view-mode toggle (only when open)
    if (!m_shaded && m_hasFiles) {
        const QRectF vr = viewToggleRect();
        if (m_viewMode == 0) { // currently icons -> "list" hint (rows)
            painter->setPen(QPen(headerText, 1.6));
            for (int i = 0; i < 3; ++i) {
                const qreal y = vr.top() + 3 + i * 5.0;
                painter->drawLine(QPointF(vr.left() + 2, y), QPointF(vr.right() - 2, y));
            }
        } else { // currently detail -> "grid" hint (cells)
            painter->setPen(Qt::NoPen);
            painter->setBrush(headerText);
            for (int gx = 0; gx < 2; ++gx)
                for (int gy = 0; gy < 2; ++gy)
                    painter->drawRect(QRectF(vr.left() + 2 + gx * 8, vr.top() + 2 + gy * 8, 5, 5));
        }
    }

    // collapse / expand toggle (child graph nodes)
    if (m_hasChildren) {
        const QRectF tr2 = collapseToggleRect();
        painter->setBrush(headerText);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(tr2, 3, 3);
        painter->setPen(QPen(header, 2));
        const QPointF c = tr2.center();
        painter->drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
        if (m_collapsed)
            painter->drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
    }

    // Stats summary when shaded (or when there are no files to show)
    if (m_shaded || !m_hasFiles) {
        QFont body2 = painter->font();
        body2.setBold(false);
        painter->setFont(body2);
        painter->setPen(text);
        painter->drawText(QRectF(8, HeaderH + 2, m_width - 16, LineH), Qt::AlignVCenter, m_stats1);
        if (!m_stats2.isEmpty()) {
            QColor muted = text;
            muted.setAlpha(170);
            painter->setPen(muted);
            painter->drawText(QRectF(8, HeaderH + 2 + LineH, m_width - 16, LineH), Qt::AlignVCenter,
                              m_stats2);
        }
    }

    // Border
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(border, isSelected() ? 2.0 : 1.0));
    painter->drawPath(body);

    // Resize grip (only when open)
    if (!m_shaded && m_proxy) {
        const QRectF h = resizeHandleRect();
        QColor grip = text;
        grip.setAlpha(120);
        painter->setPen(QPen(grip, 1.5));
        for (int i = 1; i <= 3; ++i) {
            const qreal off = i * 4.0;
            painter->drawLine(QPointF(h.right() - off, h.bottom()),
                              QPointF(h.right(), h.bottom() - off));
        }
    }
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    const QPointF p = event->pos();
    if (!m_shaded && m_hasFiles && viewToggleRect().contains(p)) {
        m_viewMode = m_viewMode == 0 ? 1 : 0;
        m_stack->setCurrentIndex(m_viewMode);
        update();
        event->accept();
        return;
    }
    if (m_hasFiles && shadeToggleRect().contains(p)) {
        toggleShade();
        event->accept();
        return;
    }
    if (m_hasChildren && collapseToggleRect().contains(p)) {
        m_onToggle(m_node);
        event->accept();
        return;
    }
    if (!m_shaded && m_proxy && resizeHandleRect().contains(p)) {
        m_resizing = true;
        event->accept();
        return;
    }
    if (m_scene)
        m_scene->setDragged(m_node); // pin so physics doesn't fight the drag
    QGraphicsItem::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (m_resizing) {
        prepareGeometryChange();
        m_width = std::max<qreal>(MinWidth, event->pos().x());
        m_openListH = std::max<qreal>(MinListH, event->pos().y() - HeaderH - 2 * kPad);
        recomputeHeight();
        updateListGeometry();
        update();
        if (m_scene)
            m_scene->onNodeMoved();
        event->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (m_resizing) {
        m_resizing = false;
        event->accept();
        return;
    }
    if (m_scene)
        m_scene->clearDragged();
    QGraphicsItem::mouseReleaseEvent(event);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && m_scene)
        m_scene->onNodeMoved();
    return QGraphicsItem::itemChange(change, value);
}

} // namespace ui
