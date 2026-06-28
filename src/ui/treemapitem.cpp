#include "treemapitem.h"

#include "core/fsnode.h"
#include "core/group.h"
#include "filetypestyle.h"
#include "graphscene.h"

#include <algorithm>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>
#include <QTransform>

namespace ui {

namespace {
// On-screen (device-pixel) thresholds — the whole point of semantic zoom is that
// detail is decided by how big a cell is on screen, not by a fixed tree depth.
constexpr double kMinDevPx = 3.0;    // smaller than this: don't bother drawing
constexpr double kSubdivW = 150.0;   // subdivide into children only once this wide…
constexpr double kSubdivH = 64.0;    // …and this tall on screen
constexpr double kLabelW = 42.0;     // room to show a name
constexpr double kLabelH = 14.0;
constexpr double kHeaderPx = 16.0;   // device-px label strip atop a subdivided cell
constexpr double kPadPx = 2.0;       // device-px inset around a child block

// Unified per-file glyph layout (ADR-301). Every LOD rung that packs a directory's
// files into its cell — the icon grid, the finer pixel-dot grid, and eventually
// filename rows — shares ONE definition of glyph size + gap and one packing helper,
// so the rungs stay visually consistent and the spacing lives in a single tunable
// place (a runtime control could drive these later instead of constants).
struct GlyphGrid {
    qreal size;                              // glyph edge, device px
    qreal gap;                               // space between glyphs, device px
    qreal pitch() const { return size + gap; }
};
constexpr GlyphGrid kIconGlyph{18.0, 8.0};  // file icons (pitch 26)
constexpr GlyphGrid kPixelGlyph{3.0, 2.0};  // pixel-dot density (pitch 5)
constexpr GlyphGrid kNameGlyph{11.0, 3.0};  // filename text rows (pitch 14, full width)
constexpr double kNameW = 90.0;             // min cell width to bother with filename rows

// How many columns/rows of `g` fit in `area`, and how many of `count` items to draw
// (capped to capacity). Last glyph never overflows: (cols-1)*pitch + size <= width.
struct GridFit {
    int cols, rows, count;
};
GridFit fitGlyphs(const QRectF &area, const GlyphGrid &g, int count) {
    const int cols = std::max(1, static_cast<int>((area.width() + g.gap) / g.pitch()));
    const int rows = std::max(1, static_cast<int>((area.height() + g.gap) / g.pitch()));
    return {cols, rows, std::min(count, cols * rows)};
}

// Squarified treemap (Bruls/Huizing/van Wijk): lay `weights` into `bounds` with
// area ∝ weight and aspect ratios kept near 1. Rects returned in input order.
std::vector<QRectF> squarify(const std::vector<double> &weights, const QRectF &bounds) {
    const int n = static_cast<int>(weights.size());
    std::vector<QRectF> out(n);
    double total = 0.0;
    for (double w : weights)
        total += w;
    if (n == 0 || total <= 0.0 || bounds.width() <= 0.0 || bounds.height() <= 0.0)
        return out;

    const double scale = (bounds.width() * bounds.height()) / total;
    std::vector<double> area(n);
    for (int i = 0; i < n; ++i)
        area[i] = weights[i] * scale;

    auto worst = [](double sum, double mn, double mx, double side) {
        if (sum <= 0.0 || side <= 0.0)
            return 1e300;
        const double s2 = side * side, sum2 = sum * sum;
        return std::max(s2 * mx / sum2, sum2 / (s2 * mn));
    };

    double x = bounds.x(), y = bounds.y(), w = bounds.width(), h = bounds.height();
    int i = 0;
    while (i < n) {
        const double side = std::min(w, h);
        int j = i;
        double sum = area[i], mn = area[i], mx = area[i], cur = worst(sum, mn, mx, side);
        while (j + 1 < n) {
            const double a = area[j + 1];
            const double nsum = sum + a, nmn = std::min(mn, a), nmx = std::max(mx, a);
            const double nworst = worst(nsum, nmn, nmx, side);
            if (nworst > cur)
                break;
            sum = nsum;
            mn = nmn;
            mx = nmx;
            cur = nworst;
            ++j;
        }
        const double thickness = sum / side;
        if (w >= h) {
            double cy = y;
            for (int k = i; k <= j; ++k) {
                const double ch = area[k] / thickness;
                out[k] = QRectF(x, cy, thickness, ch);
                cy += ch;
            }
            x += thickness;
            w -= thickness;
        } else {
            double cx = x;
            for (int k = i; k <= j; ++k) {
                const double cw = area[k] / thickness;
                out[k] = QRectF(cx, y, cw, thickness);
                cx += cw;
            }
            y += thickness;
            h -= thickness;
        }
        i = j + 1;
    }
    return out;
}

// Standard perceptually-uniform data-viz ramps, each as 8 RGB control points
// (low→high), linearly interpolated. Compact approximations of the matplotlib /
// turbo ramps — close enough for categorical depth shading.
const unsigned char kRamps[5][8][3] = {
    // Viridis
    {{68, 1, 84}, {72, 40, 120}, {62, 74, 137}, {49, 104, 142},
     {38, 130, 142}, {31, 158, 137}, {53, 183, 121}, {253, 231, 37}},
    // Magma
    {{0, 0, 4}, {28, 16, 68}, {79, 18, 123}, {129, 37, 129},
     {181, 54, 122}, {229, 80, 100}, {251, 135, 97}, {252, 253, 191}},
    // Plasma
    {{13, 8, 135}, {84, 2, 163}, {139, 10, 165}, {185, 50, 137},
     {219, 92, 104}, {244, 136, 73}, {254, 188, 43}, {240, 249, 33}},
    // Cividis
    {{0, 32, 76}, {0, 51, 110}, {57, 72, 107}, {87, 93, 109},
     {112, 113, 115}, {138, 135, 121}, {180, 159, 105}, {255, 234, 70}},
    // Turbo
    {{48, 18, 59}, {64, 91, 217}, {30, 192, 211}, {76, 240, 110},
     {178, 242, 48}, {251, 128, 34}, {210, 49, 26}, {122, 4, 3}},
};
const char *const kRampNames[] = {"Viridis", "Magma", "Plasma", "Cividis", "Turbo", "Spectrum"};

// Identity colour for a nesting depth under the chosen ramp. t spans depth 0..6.
QColor rampColor(int ramp, int depth) {
    if (ramp == 5) // Spectrum: categorical HSL hue cycle
        return QColor::fromHsl((205 + depth * 41) % 360, 120, 120);
    const double t = std::clamp(depth / 6.0, 0.0, 1.0);
    const double f = t * 7.0; // 8 stops → 7 intervals
    const int i = std::min(static_cast<int>(f), 6);
    const double frac = f - i;
    const auto &a = kRamps[ramp][i];
    const auto &b = kRamps[ramp][i + 1];
    return QColor(static_cast<int>(a[0] + (b[0] - a[0]) * frac),
                  static_cast<int>(a[1] + (b[1] - a[1]) * frac),
                  static_cast<int>(a[2] + (b[2] - a[2]) * frac));
}

QColor textColorFor(const QColor &bg) {
    return bg.lightness() < 140 ? QColor(238, 238, 238) : QColor(18, 18, 18);
}

} // namespace

TreemapItem::TreemapItem(const core::FsNode *root, qreal width, qreal height, SizeMetric metric,
                         Ramp ramp, GraphScene *scene)
    : m_root(root), m_w(width), m_h(height), m_metric(metric), m_ramp(ramp), m_scene(scene) {
    setAcceptedMouseButtons(Qt::LeftButton);
}

QColor TreemapItem::depthColor(Ramp ramp, int depth) {
    return rampColor(static_cast<int>(ramp), depth);
}

double TreemapItem::weight(const core::FsNode *n) const {
    const auto it = m_weight.find(n);
    if (it != m_weight.end())
        return it->second;
    double w = m_metric == Bytes ? static_cast<double>(n->sizeBytes) : n->fileCount;
    for (const auto &c : n->children)
        w += weight(c.get());
    w = std::max(w, 1.0); // floor so empty dirs still get a sliver
    m_weight[n] = w;
    return w;
}

void TreemapItem::setReveal(qreal factor) {
    m_reveal = std::max<qreal>(0.05, factor);
    update(); // gates are evaluated in paint(), so a repaint is all it takes
}

void TreemapItem::setDetail(qreal factor) {
    m_detail = std::max<qreal>(0.05, factor);
    update();
}

void TreemapItem::setFileMode(int mode) {
    m_fileMode = mode;
    update(); // the rung is chosen in paint()
}

void TreemapItem::setGroupStore(const core::GroupStore *store) {
    m_groups = store;
    update(); // overlay is decided in paint()
}

QRectF TreemapItem::cellRectForNode(const core::FsNode *target) const {
    if (!target)
        return {};
    // Path root..target via parent pointers (the tree this item renders).
    std::vector<const core::FsNode *> path;
    for (const core::FsNode *n = target; n; n = n->parent) {
        path.push_back(n);
        if (n == m_root)
            break;
    }
    if (path.empty() || path.back() != m_root)
        return {}; // target isn't under this root
    std::reverse(path.begin(), path.end());

    // Replay the same subdivision as drawCell, in item space (zoom=1 approximation:
    // the device-constant header/pad insets are small, so the cone anchor is close
    // enough). Descend rect → child rect along the path.
    QRectF rect(0, 0, m_w, m_h);
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const core::FsNode *node = path[i];
        std::vector<const core::FsNode *> kids;
        kids.reserve(node->children.size());
        for (const auto &c : node->children)
            kids.push_back(c.get());
        std::sort(kids.begin(), kids.end(), [this](const core::FsNode *a, const core::FsNode *b) {
            return weight(a) > weight(b);
        });
        // Match drawCell's device-space insets (kHeaderPx/zoom, kPadPx/zoom) so the
        // replayed rect lines up with the actually-drawn square at the current zoom.
        const qreal hdr = kHeaderPx / m_lastZoom, pad = kPadPx / m_lastZoom;
        const QRectF inner = rect.adjusted(pad, hdr, -pad, -pad);
        std::vector<double> ws;
        ws.reserve(kids.size());
        for (const auto *k : kids)
            ws.push_back(weight(k));
        const std::vector<QRectF> rects = squarify(ws, inner);
        int idx = -1;
        for (size_t k = 0; k < kids.size(); ++k)
            if (kids[k] == path[i + 1]) {
                idx = static_cast<int>(k);
                break;
            }
        if (idx < 0)
            return {};
        rect = rects[idx];
    }
    return rect;
}

void TreemapItem::setSize(qreal width, qreal height) {
    if (width <= 0 || height <= 0 || (qFuzzyCompare(width, m_w) && qFuzzyCompare(height, m_h)))
        return;
    prepareGeometryChange();
    m_w = width;
    m_h = height;
    update(); // squarify happens in paint() against m_w/m_h
}

QRectF TreemapItem::boundingRect() const {
    return QRectF(0, 0, m_w, m_h);
}

void TreemapItem::drawCell(QPainter *p, const core::FsNode *node, const QRectF &rect, int depth,
                           const QTransform &toDevice, const QRectF &exposed) const {
    if (!rect.intersects(exposed))
        return; // off-screen — cull
    const QRectF dev = toDevice.mapRect(rect);
    if (dev.width() < kMinDevPx || dev.height() < kMinDevPx)
        return; // too small on screen to be worth a cell

    m_cells.push_back({rect, node});
    const double zoom = toDevice.m11();
    // m_reveal gates subdivision (how deep nesting shows); m_detail gates the
    // contents crossover below — kept separate so revealing deeper nesting doesn't
    // also promote small squares from pixel-dots into full icons, and vice versa.
    const bool subdivide = !node->children.empty() && dev.width() > kSubdivW * m_reveal &&
                           dev.height() > kSubdivH * m_reveal;

    // Every cell = a title bar (the ramp identity colour) over a contents area (a
    // darker value in dark mode / lighter in light mode), so icons and child cells
    // sit on a low-key background and read clearly.
    const QColor title = rampColor(m_ramp, depth);
    const QColor body = m_dark ? title.darker(235) : title.lighter(168);
    const bool hasTitle = dev.width() > kLabelW * m_detail && dev.height() > kHeaderPx * 1.5 * m_detail;

    // Semantic-group overlay (ADR-102): highlight tints + borders a group's cells;
    // focus dims non-members; dim de-emphasises a group's own members.
    bool ovHighlight = false;
    QColor ovColor;
    bool ovDim = false;
    if (m_groups && !m_groups->empty()) {
        // Hot path: drawCell runs for every visible cell every frame, so iterate the
        // groups directly (no per-cell allocation — groupsContaining() would heap a
        // vector here). g->contains() is two QSet lookups.
        const core::MemberKey key = core::keyFor(*node);
        bool focusMember = false, dimMember = false;
        for (const auto &g : m_groups->groups()) {
            if (!g->view.visible || !g->contains(key))
                continue;
            // First highlighted group in store order wins the tint/border colour;
            // others contribute nothing — deterministic, no blending by design.
            if (g->view.highlight && !ovHighlight) {
                ovHighlight = true;
                ovColor = g->color;
            }
            focusMember = focusMember || g->view.focus;
            dimMember = dimMember || g->view.dim;
        }
        ovDim = dimMember || (m_anyFocus && !focusMember);
    }
    // Drawn over this cell's own area after its content; children overdraw the inner
    // region and dim themselves, so members stay bright while non-members recede.
    auto dimScrim = [&] {
        if (ovDim)
            p->fillRect(rect, m_dark ? QColor(0, 0, 0, 150) : QColor(255, 255, 255, 150));
    };

    p->fillRect(rect, body);
    if (ovHighlight) {
        QColor tint = ovColor;
        tint.setAlpha(90);
        p->fillRect(rect, tint);
    }
    if (hasTitle) {
        p->fillRect(QRectF(rect.x(), rect.y(), rect.width(), kHeaderPx / zoom), title);
        p->setWorldMatrixEnabled(false);
        QFont f = p->font();
        f.setPixelSize(11);
        p->setFont(f);
        p->setPen(textColorFor(title));
        p->drawText(QRectF(dev.x() + 4, dev.y(), dev.width() - 6, kHeaderPx),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(f).elidedText(node->name, Qt::ElideMiddle,
                                               static_cast<int>(dev.width() - 6)));
        p->setWorldMatrixEnabled(true);
    }

    // Highlighted cells get a thicker group-colour frame; others the plain hairline.
    QPen border = ovHighlight ? QPen(ovColor, 2.0) : QPen(QColor(0, 0, 0, 160));
    border.setCosmetic(true); // constant width regardless of zoom
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRect(rect);
    if (node == m_selected) {
        QPen sel(qApp ? qApp->palette().color(QPalette::Highlight) : QColor(120, 170, 255), 2.0);
        sel.setCosmetic(true);
        p->setPen(sel);
        p->drawRect(rect);
    }

    if (subdivide) {
        const qreal hdr = (hasTitle ? kHeaderPx : kPadPx) / zoom, pad = kPadPx / zoom;
        const QRectF inner = rect.adjusted(pad, hdr, -pad, -pad);
        std::vector<const core::FsNode *> kids;
        kids.reserve(node->children.size());
        for (const auto &c : node->children)
            kids.push_back(c.get());
        std::sort(kids.begin(), kids.end(),
                  [this](const core::FsNode *a, const core::FsNode *b) { return weight(a) > weight(b); });
        std::vector<double> ws;
        ws.reserve(kids.size());
        for (const auto *k : kids)
            ws.push_back(weight(k));
        const std::vector<QRectF> rects = squarify(ws, inner);
        dimScrim(); // dim this cell's chrome; children overdraw the inner and self-dim
        for (size_t k = 0; k < kids.size(); ++k)
            drawCell(p, kids[k], rects[k], depth + 1, toDevice, exposed);
        return;
    }

    drawLeafContents(p, node, dev, hasTitle, body);
    dimScrim(); // leaf: dim the whole cell (body + contents) when de-emphasised
}

// The leaf rung: a cell's files drawn as a list (icon+name), icons, or pixel-dots
// (or the cell's own name when it has none). The rung is chosen by cell size (Detail
// LOD) in Auto,
// or forced by m_fileMode. All three colour-match the file type (shared
// fileTypeColor / fileTypeIcon). Each helper self-guards on room, so a forced rung
// on a too-small cell simply draws nothing.
void TreemapItem::drawLeafContents(QPainter *p, const core::FsNode *node, const QRectF &dev,
                                   bool hasTitle, const QColor &body) const {
    const QRectF area = dev.adjusted(3, (hasTitle ? kHeaderPx : 2.0), -2, -2);

    auto drawIcons = [&] {
        if (area.width() < kIconGlyph.size || area.height() < kIconGlyph.size)
            return;
        p->setWorldMatrixEnabled(false);
        const GridFit fit = fitGlyphs(area, kIconGlyph, static_cast<int>(node->files.size()));
        for (int i = 0; i < fit.count; ++i) {
            const int r = i / fit.cols, c = i % fit.cols;
            const QRect ir(static_cast<int>(area.x() + c * kIconGlyph.pitch()),
                           static_cast<int>(area.y() + r * kIconGlyph.pitch()),
                           static_cast<int>(kIconGlyph.size), static_cast<int>(kIconGlyph.size));
            fileTypeIcon(node->files[i]).paint(p, ir);
        }
        p->setWorldMatrixEnabled(true);
    };
    auto drawDots = [&] {
        const GlyphGrid &g = kPixelGlyph;
        if (area.width() < g.size || area.height() < g.size)
            return;
        p->setWorldMatrixEnabled(false);
        const GridFit fit = fitGlyphs(area, g, static_cast<int>(node->files.size()));
        for (int i = 0; i < fit.count; ++i) {
            const int r = i / fit.cols, c = i % fit.cols;
            p->fillRect(QRectF(area.x() + c * g.pitch(), area.y() + r * g.pitch(), g.size, g.size),
                        fileTypeColor(node->files[i]));
        }
        p->setWorldMatrixEnabled(true);
    };
    auto drawList = [&] {
        // Multi-column icon + name grid, filled column-major like `ls -a`, so a tall
        // cell uses its full width instead of one wasteful column. Name colour-matches
        // the type; the icon is the same theme icon as the Icons rung, just inline.
        constexpr qreal kIcon = 13.0, kIconGap = 3.0, kColW = 150.0; // device px
        const qreal rowH = kNameGlyph.pitch();
        if (area.height() < rowH || area.width() < kIcon + 8.0)
            return;
        p->setWorldMatrixEnabled(false);
        QFont f = p->font();
        f.setPixelSize(10);
        p->setFont(f);
        const QFontMetrics fm(f);
        const int cols = std::max(1, static_cast<int>(area.width() / kColW));
        const int rows = std::max(1, static_cast<int>(area.height() / rowH));
        const double colW = area.width() / cols; // distribute evenly across the width
        const int nf = std::min(static_cast<int>(node->files.size()), cols * rows);
        for (int i = 0; i < nf; ++i) {
            const int col = i / rows, row = i % rows; // column-major, like ls
            const double x = area.x() + col * colW, y = area.y() + row * rowH;
            const QString &name = node->files[i];
            const QRect ir(static_cast<int>(x), static_cast<int>(y + (rowH - kIcon) / 2),
                           static_cast<int>(kIcon), static_cast<int>(kIcon));
            fileTypeIcon(name).paint(p, ir);
            p->setPen(fileTypeColor(name));
            const QRectF tr(x + kIcon + kIconGap, y, colW - kIcon - kIconGap - 4.0, rowH);
            p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                        fm.elidedText(name, Qt::ElideMiddle, static_cast<int>(tr.width())));
        }
        p->setWorldMatrixEnabled(true);
    };
    auto drawDirName = [&] {
        p->setWorldMatrixEnabled(false);
        QFont f = p->font();
        f.setPixelSize(11);
        p->setFont(f);
        p->setPen(textColorFor(body));
        p->drawText(dev.adjusted(3, 2, -3, -2), Qt::AlignCenter,
                    QFontMetrics(f).elidedText(node->name, Qt::ElideMiddle,
                                               static_cast<int>(dev.width() - 6)));
        p->setWorldMatrixEnabled(true);
    };

    const bool hasFiles = !node->files.isEmpty();
    const bool forced = m_fileMode != Auto;
    const bool listFit = dev.width() > kNameW * m_detail &&
                         dev.height() > (kHeaderPx + kNameGlyph.pitch() * 2) * m_detail;
    const bool iconsFit = dev.width() > 70.0 * m_detail &&
                          dev.height() > (kHeaderPx + kIconGlyph.pitch()) * m_detail;
    const bool labelFits = !hasTitle && dev.width() > kLabelW * m_detail &&
                           dev.height() > kLabelH * m_detail;
    if (hasFiles && (m_fileMode == List || (!forced && listFit)))
        drawList();
    else if (hasFiles && (m_fileMode == Icons || (!forced && iconsFit)))
        drawIcons();
    else if (hasFiles && m_fileMode == Dots)
        drawDots();
    else if (labelFits)
        drawDirName(); // the cell's own name (Auto fallback, or a fileless dir)
    else if (hasFiles)
        drawDots(); // Auto floor: density dots where even the name won't fit
}

void TreemapItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) {
    painter->setRenderHint(QPainter::Antialiasing, false); // crisp rectangle edges
    m_cells.clear();
    m_dark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
    m_anyFocus = false; // focus mode dims every non-member; resolve it once per paint
    if (m_groups)
        for (const auto &g : m_groups->groups())
            if (g->view.visible && g->view.focus) {
                m_anyFocus = true;
                break;
            }
    const QTransform toDevice = painter->worldTransform();
    m_lastZoom = toDevice.m11() > 0 ? toDevice.m11() : 1.0; // for cellRectForNode insets
    const QRectF exposed = option ? option->exposedRect : QRectF(0, 0, m_w, m_h);
    drawCell(painter, m_root, QRectF(0, 0, m_w, m_h), 0, toDevice, exposed);
}

const core::FsNode *TreemapItem::cellAt(const QPointF &p) const {
    const core::FsNode *hit = nullptr;
    for (const Cell &c : m_cells) // pre-order: last match is the deepest
        if (c.rect.contains(p))
            hit = c.node;
    return hit;
}

void TreemapItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    const core::FsNode *n = cellAt(event->pos());
    if (n) {
        m_selected = n; // left click = select (drag-to-reparent comes later)
        update();
        event->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(event);
}

void TreemapItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    // ADR-303: double-click opens a floating investigation frame on that subtree
    // (a non-destructive lens) instead of re-rooting the map. Find the deepest cell
    // under the cursor and hand the scene both the node and its rect (mapped to
    // scene space) so the frame can anchor its callout lines to the origin square.
    const core::FsNode *hit = nullptr;
    QRectF hitRect;
    for (const Cell &c : m_cells) // pre-order: last match is the deepest
        if (c.rect.contains(event->pos())) {
            hit = c.node;
            hitRect = c.rect;
        }
    // Open only for a node strictly *deeper* than this treemap's own root: never the
    // root itself (that re-opens the same subtree, stacking identical frames), and
    // only when there are deeper levels to show — otherwise ignore the double-click.
    if (hit && hit != m_root && !hit->children.empty() && m_scene) {
        m_scene->openFrame(hit, mapToScene(hitRect).boundingRect(), m_ownerFrame);
        event->accept();
        return;
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
}

} // namespace ui
