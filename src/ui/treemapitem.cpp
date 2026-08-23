// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treemapitem.h"

#include "core/fsnode.h"
#include "core/group.h"
#include "filetypestyle.h"
#include "frameitem.h"
#include "graphscene.h"
#include "treemaplayout.h"

#include <algorithm>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFileDevice>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QCache>
#include <QStaticText>
#include <QStyleOptionGraphicsItem>
#include <QTransform>

namespace ui {

namespace {
// On-screen (device-pixel) thresholds — the whole point of semantic zoom is that
// detail is decided by how big a cell is on screen, not by a fixed tree depth.
// Geometry thresholds (cell size, subdivision, header/pad insets) live in
// TreemapLayout so the painter and the layout can't disagree; these are the
// contents-only gates.
constexpr double kLabelW = TreemapLayout::kLabelW;
constexpr double kLabelH = 14.0;
constexpr double kHeaderPx = TreemapLayout::kHeaderPx;

// Unified per-file glyph layout (ADR-301). Every LOD rung that packs a directory's
// files into its cell — the icon grid, the finer pixel-dot grid, and eventually
// filename rows — shares ONE definition of glyph size + gap and one packing helper,
// so the rungs stay visually consistent and the spacing lives in a single tunable
// place (a runtime control could drive these later instead of constants).
struct GlyphGrid {
    qreal size;    // glyph width (and height, unless `h` is set), device px
    qreal gap;     // space between glyphs, device px
    qreal h = 0.0; // glyph height when the glyph isn't square (0 = same as size)
    constexpr qreal height() const { return h > 0.0 ? h : size; }
    constexpr qreal pitch() const { return size + gap; }
    constexpr qreal pitchY() const { return height() + gap; }
};
constexpr GlyphGrid kIconGlyph{18.0, 8.0};        // file icons (pitch 26)
constexpr GlyphGrid kNamedGlyph{48.0, 6.0, 40.0}; // icon + name beneath (pitch 54 × 46)
constexpr GlyphGrid kPixelGlyph{3.0, 2.0};        // pixel-dot density (pitch 5)
constexpr GlyphGrid kNameGlyph{11.0, 3.0};        // filename text rows (pitch 14, full width)
constexpr double kNameW = 90.0;                   // min cell width to bother with filename rows

// The file-rung table (#44): ONE place that lists the rungs, richest → poorest, and
// what each needs. The numbers are AUTO-QUALITY GATES, not physical fit: the on-screen
// cell size (header included, device px, × the Detail factor) at which Auto considers
// the rung worth showing. Auto walks top-down and takes the first row whose gates pass
// and whose painter draws; rows flagged forceOnly or autoSkip are stepped over. A
// FORCED mode ignores the gates entirely: it starts at its own row and walks down,
// trying each painter in turn, so the painter's own fit guard (does the content area
// physically hold one glyph / one row?) is the only thing that sends it to the next
// rung — no Detail scaling, no blank cell. Below the table sits the dir-name / dots
// floor in drawLeafContents.
//   Details    force-only: `ls -l` rows need more width than any Auto cell earns; its
//              painter measures the meta column and guards on that, so the gates are
//              moot and left at 0.
//   List       outranks IconsNamed on purpose — one 14px row per file shows more
//              legible names per cell than a 48×40 tile does.
//   IconsNamed the forced icon view ("Files: Icons"); Auto reaches it only for cells
//              too narrow for List yet tall enough for a tile (w in (70, 90], h > 62).
//   Icons      the bare grid, Auto-only: the step below a tile when height runs out.
//   Dots       autoSkip: in Auto the cell's own name outranks dots, so that choice is
//              the floor below the table; the row exists so forced Dots starts here.
struct RungSpec {
    TreemapItem::FileMode mode;
    double minW, minH; // Auto gate: min cell size on screen, device px (× Detail)
    bool forceOnly;    // Auto never picks it; reached by forcing it (or falling past)
    bool autoSkip;     // Auto never picks it; the floor handles the case instead
};
constexpr RungSpec kRungs[] = {
    {TreemapItem::Details, 0.0, 0.0, true, false},
    {TreemapItem::List, kNameW, kHeaderPx + kNameGlyph.pitch() * 2, false, false},
    {TreemapItem::IconsNamed, 70.0, kHeaderPx + kNamedGlyph.pitchY(), false, false},
    {TreemapItem::Icons, 70.0, kHeaderPx + kIconGlyph.pitch(), false, false},
    {TreemapItem::Dots, kPixelGlyph.size, kPixelGlyph.size, false, true},
};

// How many columns/rows of `g` fit in `area`, and how many of `count` items to draw
// (capped to capacity). Last glyph never overflows: (cols-1)*pitch + size <= width.
struct GridFit {
    int cols, rows, count;
};
GridFit fitGlyphs(const QRectF &area, const GlyphGrid &g, int count) {
    const int cols = std::max(1, static_cast<int>((area.width() + g.gap) / g.pitch()));
    const int rows = std::max(1, static_cast<int>((area.height() + g.gap) / g.pitchY()));
    return {cols, rows, std::min(count, cols * rows)};
}

// `ls -l`-style permission string for a file entry: a 10-char "lrwxr-xr-x" — leading
// type char ('l' symlink, else '-'), then owner/group/other rwx triples decoded from
// the stored QFileDevice::Permissions bits.
QString permString(const core::FileEntry &fe) {
    const QFileDevice::Permissions p(QFlag(static_cast<int>(fe.perms)));
    auto bit = [&](QFileDevice::Permission b, char c) {
        return (p & b) ? QLatin1Char(c) : QLatin1Char('-');
    };
    QString s;
    s.reserve(10);
    s += fe.isSymlink ? QLatin1Char('l') : QLatin1Char('-');
    s += bit(QFileDevice::ReadOwner, 'r');
    s += bit(QFileDevice::WriteOwner, 'w');
    s += bit(QFileDevice::ExeOwner, 'x');
    s += bit(QFileDevice::ReadGroup, 'r');
    s += bit(QFileDevice::WriteGroup, 'w');
    s += bit(QFileDevice::ExeGroup, 'x');
    s += bit(QFileDevice::ReadOther, 'r');
    s += bit(QFileDevice::WriteOther, 'w');
    s += bit(QFileDevice::ExeOther, 'x');
    return s;
}

// The fixed-width metadata column for a Details row: "perms  size  mtime", with size
// human-readable and right-aligned (left-padded) so columns line up under a monospace
// font. Width 10 covers the widest traditional sub-KiB form ("1023 bytes"). mtime 0
// (unknown) renders as dashes.
QString detailMeta(const core::FileEntry &fe) {
    const QString size =
        QLocale::system().formattedDataSize(fe.sizeBytes, 1, QLocale::DataSizeTraditionalFormat);
    const QString when =
        fe.mtime
            ? QDateTime::fromSecsSinceEpoch(fe.mtime).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : QStringLiteral("     -          ");
    return QStringLiteral("%1 %2 %3  ").arg(permString(fe)).arg(size, 10).arg(when);
}

// A file/dir name elided to `width`, laid out once and cached. Elision + text layout
// per name per frame was the largest single paint cost at large window sizes; names
// repeat heavily across a tree (README.md, .gitignore…) and the width is bucketed, so
// the cache stays small and hits almost always. Keyed on font pixel size too, since
// the rungs use 10px and the title 11px.
QStaticText elidedName(const QFont &font, const QString &name, qreal width) {
    // Bounded: a continuous zoom walks every name through many width buckets, and
    // each entry holds a prepared QTextLayout. LRU eviction keeps the working set.
    static QCache<QString, QStaticText> cache(4096);
    const int bucket = static_cast<int>(std::max<qreal>(0.0, width) / 6.0); // 6px steps
    const QString key = QString::number(font.pixelSize()) + QLatin1Char('|') +
                        QString::number(bucket) + QLatin1Char('|') + name;
    if (const QStaticText *hit = cache.object(key))
        return *hit; // implicitly shared: a copy is a refcount bump
    auto *st = new QStaticText(QFontMetrics(font).elidedText(name, Qt::ElideMiddle, bucket * 6));
    st->setTextFormat(Qt::PlainText);
    st->setPerformanceHint(QStaticText::AggressiveCaching);
    st->prepare(QTransform(), font);
    cache.insert(key, st);
    return *st;
}

// Standard perceptually-uniform data-viz ramps, each as 8 RGB control points
// (low→high), linearly interpolated. Compact approximations of the matplotlib /
// turbo ramps — close enough for categorical depth shading.
const unsigned char kRamps[5][8][3] = {
    // Viridis
    {{68, 1, 84},
     {72, 40, 120},
     {62, 74, 137},
     {49, 104, 142},
     {38, 130, 142},
     {31, 158, 137},
     {53, 183, 121},
     {253, 231, 37}},
    // Magma
    {{0, 0, 4},
     {28, 16, 68},
     {79, 18, 123},
     {129, 37, 129},
     {181, 54, 122},
     {229, 80, 100},
     {251, 135, 97},
     {252, 253, 191}},
    // Plasma
    {{13, 8, 135},
     {84, 2, 163},
     {139, 10, 165},
     {185, 50, 137},
     {219, 92, 104},
     {244, 136, 73},
     {254, 188, 43},
     {240, 249, 33}},
    // Cividis
    {{0, 32, 76},
     {0, 51, 110},
     {57, 72, 107},
     {87, 93, 109},
     {112, 113, 115},
     {138, 135, 121},
     {180, 159, 105},
     {255, 234, 70}},
    // Turbo
    {{48, 18, 59},
     {64, 91, 217},
     {30, 192, 211},
     {76, 240, 110},
     {178, 242, 48},
     {251, 128, 34},
     {210, 49, 26},
     {122, 4, 3}},
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
    m_layout.setRoot(root);
    // Without this, option->exposedRect is just boundingRect(): the exposed-rect cull
    // in drawCell never culled, and leaf contents couldn't anchor to the viewport.
    setFlag(ItemUsesExtendedStyleOption, true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

QColor TreemapItem::depthColor(Ramp ramp, int depth) {
    return rampColor(static_cast<int>(ramp), depth);
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

void TreemapItem::setInteracting(bool on) {
    if (m_interacting == on)
        return;
    m_interacting = on;
    update();
}

void TreemapItem::invalidateLayout() {
    m_layout.invalidate();
    update();
}

void TreemapItem::setGroupStore(const core::GroupStore *store) {
    m_groups = store;
    update(); // overlay is decided in paint()
}

QRectF TreemapItem::cellRectForNode(const core::FsNode *target) const {
    return m_layout.rectFor(target); // cached cell, or replayed for a culled node
}

void TreemapItem::setSize(qreal width, qreal height) {
    if (width <= 0 || height <= 0 || (qFuzzyCompare(width, m_w) && qFuzzyCompare(height, m_h)))
        return;
    prepareGeometryChange();
    m_w = width;
    m_h = height;
    update(); // paint() re-ensures the layout against m_w/m_h
}

QRectF TreemapItem::boundingRect() const {
    return QRectF(0, 0, m_w, m_h);
}

void TreemapItem::drawCell(QPainter *p, int index, const QTransform &toDevice,
                           const QRectF &exposed) const {
    const LayoutCell &cell = m_layout.cells()[static_cast<std::size_t>(index)];
    const QRectF &rect = cell.rect;
    if (!rect.intersects(exposed))
        return; // off-screen — cull (the layout is zoom-keyed, so this is the only per-pan test)
    const core::FsNode *node = cell.node;
    const QRectF dev = toDevice.mapRect(rect);
    const double zoom = toDevice.m11();
    const bool subdivide = cell.subdivided;
    const bool hasTitle = cell.hasTitle;

    // Every cell = a title bar (the ramp identity colour) over a contents area (a
    // darker value in dark mode / lighter in light mode), so icons and child cells
    // sit on a low-key background and read clearly.
    const QColor title = rampColor(m_ramp, cell.depth);
    const QColor body = m_dark ? title.darker(235) : title.lighter(168);

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

    // A subdivided cell's children tile its inner rect exactly (squarify partitions
    // the area), so the body only needs painting where a child won't: the chrome rim
    // around the inner rect, and — if any child was culled for size — behind the
    // holes. Filling the whole rect every level meant each pixel of a deep map was
    // painted once per nesting level; this paints it once.
    const bool rimOnly = subdivide && !ovHighlight && !ovDim;
    if (rimOnly) {
        const QRectF &inner = cell.inner;
        p->fillRect(QRectF(rect.left(), rect.top(), rect.width(), inner.top() - rect.top()), body);
        p->fillRect(
            QRectF(rect.left(), inner.bottom(), rect.width(), rect.bottom() - inner.bottom()),
            body);
        p->fillRect(QRectF(rect.left(), inner.top(), inner.left() - rect.left(), inner.height()),
                    body);
        p->fillRect(
            QRectF(inner.right(), inner.top(), rect.right() - inner.right(), inner.height()), body);
        // Children culled for size leave holes the rim fill skipped; fill just those.
        for (const QRectF &hole : cell.holes)
            p->fillRect(hole, body);
    } else {
        p->fillRect(rect, body);
    }
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
        const QStaticText st = elidedName(f, node->name, dev.width() - 6);
        p->drawStaticText(QPointF(dev.x() + 4, dev.y() + (kHeaderPx - st.size().height()) / 2.0),
                          st);
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
        dimScrim(); // dim this cell's chrome; children overdraw the inner and self-dim
        for (int c = cell.firstChild; c >= 0;
             c = m_layout.cells()[static_cast<std::size_t>(c)].nextSibling)
            drawCell(p, c, toDevice, exposed);
        if (const int step = diffStepFor(node))
            drawDiffMark(p, dev, step); // on top of children, so a moved dir stays marked
        return;
    }

    if (cell.wantsChildren && m_scene)
        m_scene->requestDeepen(node); // the scan stopped here and the view wants more
    if (node->truncatedDepth && node->children.empty())
        drawUnscannedMark(p, hasTitle ? dev.adjusted(0, kHeaderPx, 0, 0) : dev); // more below
    if (!m_interacting)
        drawLeafContents(p, node, dev, hasTitle, body, toDevice.mapRect(exposed));
    dimScrim(); // leaf: dim the whole cell (body + contents) when de-emphasised
    if (const int step = diffStepFor(node))
        drawDiffMark(p, dev, step);
}

// The staged-move step that *actually relocated* this node, or 0 if none. A node is
// only marked when its projected path diverged from where it was scanned: an op
// projectForest skipped (collision / cycle / unresolved) leaves the node in place, so
// path == originalPath and we must not paint a false "moved" badge on it (ADR-302 #12).
// (This compares scanned-vs-projected *paths*, not identity, so it stays correct once
// identity is a durable UUID rather than the old path-as-key fallback — ADR-100.)
int TreemapItem::diffStepFor(const core::FsNode *node) const {
    if (m_diffSteps.isEmpty() || node->path == node->originalPath)
        return 0;
    return m_diffSteps.value(core::keyFor(*node), 0);
}

// The leaf rung: a cell's files drawn per the rung table kRungs — details rows,
// list (icon+name), icon+name tiles, bare icons, or pixel-dots — or the cell's own
// name when none fits (or it has no files). Auto takes the richest rung that fits
// the cell (Detail LOD); a forced m_fileMode starts at its rung and falls down the
// table. All rungs colour-match the file type (shared fileTypeColor / fileTypeIcon).
// Each painter self-guards on its content area and reports whether it drew, so a
// rung that passes the table but not its own guard still falls through.
void TreemapItem::drawLeafContents(QPainter *p, const core::FsNode *node, const QRectF &dev,
                                   bool hasTitle, const QColor &body,
                                   const QRectF &visibleDev) const {
    // The content area is the cell body clipped to what's on screen. Every rung packs
    // its glyphs from this rect's top-left, so once the view is inside a cell the
    // files sit at the viewport's corner instead of off-screen at the cell's; while
    // the cell fits in view this is the cell body exactly. The rung choice below
    // still reads the full cell size (`dev`), so it doesn't flicker with panning.
    QRectF area = dev.adjusted(3, (hasTitle ? kHeaderPx : 2.0), -2, -2);
    const QRectF onScreen = area.intersected(visibleDev.adjusted(3, 3, -3, -3));
    if (onScreen.isValid())
        area = onScreen;

    auto drawIcons = [&] {
        if (area.width() < kIconGlyph.size || area.height() < kIconGlyph.size)
            return false;
        p->setWorldMatrixEnabled(false);
        const GridFit fit = fitGlyphs(area, kIconGlyph, static_cast<int>(node->files.size()));
        for (int i = 0; i < fit.count; ++i) {
            const int r = i / fit.cols, c = i % fit.cols;
            const QPoint at(static_cast<int>(area.x() + c * kIconGlyph.pitch()),
                            static_cast<int>(area.y() + r * kIconGlyph.pitch()));
            p->drawPixmap(at,
                          fileTypePixmap(node->files[i].name, static_cast<int>(kIconGlyph.size)));
        }
        p->setWorldMatrixEnabled(true);
        return true;
    };
    auto drawIconsNamed = [&] {
        // Icon with its name elided beneath, one tile per file, grid-packed like the
        // bare icon grid but with a wider, taller glyph so the name has a line to sit
        // on. The icon is the same theme icon as the other rungs; the name colour-
        // matches the type, centred under it.
        const GlyphGrid &g = kNamedGlyph;
        if (area.width() < g.size || area.height() < g.height())
            return false;
        p->setWorldMatrixEnabled(false);
        QFont f = p->font();
        f.setPixelSize(10);
        p->setFont(f);
        const int icon = static_cast<int>(kIconGlyph.size);
        const GridFit fit = fitGlyphs(area, g, static_cast<int>(node->files.size()));
        for (int i = 0; i < fit.count; ++i) {
            const int r = i / fit.cols, c = i % fit.cols;
            const QString &name = node->files[i].name;
            const double x = area.x() + c * g.pitch(), y = area.y() + r * g.pitchY();
            p->drawPixmap(QPoint(static_cast<int>(x + (g.size - icon) / 2), static_cast<int>(y)),
                          fileTypePixmap(name, icon));
            p->setPen(fileTypeColor(name));
            const QStaticText st = elidedName(f, name, g.size);
            p->drawStaticText(QPointF(x + (g.size - st.size().width()) / 2.0, y + icon + 2.0), st);
        }
        p->setWorldMatrixEnabled(true);
        return true;
    };
    auto drawDots = [&] {
        const GlyphGrid &g = kPixelGlyph;
        if (area.width() < g.size || area.height() < g.size)
            return false;
        p->setWorldMatrixEnabled(false);
        const GridFit fit = fitGlyphs(area, g, static_cast<int>(node->files.size()));
        for (int i = 0; i < fit.count; ++i) {
            const int r = i / fit.cols, c = i % fit.cols;
            p->fillRect(QRectF(area.x() + c * g.pitch(), area.y() + r * g.pitch(), g.size, g.size),
                        fileTypeColor(node->files[i].name));
        }
        p->setWorldMatrixEnabled(true);
        return true;
    };
    auto drawList = [&] {
        // Multi-column icon + name grid, filled column-major like `ls -a`, so a tall
        // cell uses its full width instead of one wasteful column. Name colour-matches
        // the type; the icon is the same theme icon as the Icons rung, just inline.
        constexpr qreal kIcon = 13.0, kIconGap = 3.0, kColW = 150.0; // device px
        const qreal rowH = kNameGlyph.pitch();
        if (area.height() < rowH || area.width() < kIcon + 8.0)
            return false;
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
            const QString &name = node->files[i].name;
            p->drawPixmap(QPoint(static_cast<int>(x), static_cast<int>(y + (rowH - kIcon) / 2)),
                          fileTypePixmap(name, static_cast<int>(kIcon)));
            p->setPen(fileTypeColor(name));
            const QRectF tr(x + kIcon + kIconGap, y, colW - kIcon - kIconGap - 4.0, rowH);
            const QStaticText st = elidedName(f, name, tr.width());
            p->drawStaticText(QPointF(tr.x(), tr.y() + (rowH - st.size().height()) / 2.0), st);
        }
        p->setWorldMatrixEnabled(true);
        return true;
    };
    auto drawDetails = [&] {
        // One file per row with metadata, like `ls -l`: a muted fixed-width meta column
        // (perms · size · mtime) then the type-coloured icon + name. A monospace font
        // keeps the meta columns aligned across rows; needs the most width of any rung.
        const qreal rowH = kNameGlyph.pitch();
        if (area.height() < rowH)
            return false;
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPixelSize(10);
        const QFontMetrics fm(f);
        // Measure the meta column from a worst-case sample (4-digit byte size + a real
        // mtime) so it never under-measures vs. a default FileEntry; gate on it *after*
        // measuring — nothing here clips to the cell, so a too-narrow cell must fall
        // to a poorer rung rather than bleed perms/name over its neighbours.
        constexpr qreal kIcon = 12.0, kGap = 4.0, kMinName = 24.0;
        core::FileEntry sample;
        sample.sizeBytes = 1023; // widest sub-KiB form: "1023 bytes"
        sample.mtime = 1;        // force a rendered datetime, not the dashes placeholder
        const double metaW = fm.horizontalAdvance(detailMeta(sample));
        if (area.width() < metaW + kIcon + kGap + kMinName)
            return false;
        p->setWorldMatrixEnabled(false);
        p->setFont(f);
        QColor metaCol = textColorFor(body);
        metaCol.setAlpha(160); // de-emphasised next to the file name
        const int rows = std::max(1, static_cast<int>(area.height() / rowH));
        const int nf = std::min(static_cast<int>(node->files.size()), rows);
        for (int i = 0; i < nf; ++i) {
            const core::FileEntry &fe = node->files[i];
            const double y = area.y() + i * rowH;
            p->setPen(metaCol);
            p->drawText(QRectF(area.x(), y, metaW, rowH), Qt::AlignVCenter | Qt::AlignLeft,
                        detailMeta(fe));
            const double nx = area.x() + metaW;
            p->drawPixmap(QPoint(static_cast<int>(nx), static_cast<int>(y + (rowH - kIcon) / 2)),
                          fileTypePixmap(fe.name, static_cast<int>(kIcon)));
            p->setPen(fileTypeColor(fe.name));
            const QRectF tr(nx + kIcon + kGap, y, area.right() - (nx + kIcon + kGap), rowH);
            if (tr.width() > 8.0) {
                const QStaticText st = elidedName(f, fe.name, tr.width());
                p->drawStaticText(QPointF(tr.x(), tr.y() + (rowH - st.size().height()) / 2.0), st);
            }
        }
        p->setWorldMatrixEnabled(true);
        return true;
    };
    auto drawDirName = [&] {
        p->setWorldMatrixEnabled(false);
        QFont f = p->font();
        f.setPixelSize(11);
        p->setFont(f);
        p->setPen(textColorFor(body));
        const QStaticText st = elidedName(f, node->name, dev.width() - 6);
        p->save();
        p->setClipRect(dev); // a low Detail factor can hand an 11px label a 7px cell
        p->drawStaticText(dev.center() - QPointF(st.size().width() / 2.0, st.size().height() / 2.0),
                          st);
        p->restore();
        p->setWorldMatrixEnabled(true);
    };
    auto drawRung = [&](FileMode mode) {
        switch (mode) {
        case Details:
            return drawDetails();
        case List:
            return drawList();
        case IconsNamed:
            return drawIconsNamed();
        case Icons:
            return drawIcons();
        case Dots:
            return drawDots();
        case Auto:
            break;
        }
        return false;
    };

    // Walk the rung table. Auto: from the top, skipping forceOnly/autoSkip rows, the
    // first row whose Auto gates pass and whose painter draws wins. Forced: from the
    // requested row, gates ignored, the first painter that draws wins — a forceOnly
    // row passed on the way down (none today, Details is row 0) is skipped so falling
    // down never lands on a rung Auto wouldn't offer either.
    const bool forced = m_fileMode != Auto;
    if (!node->files.empty()) {
        bool started = !forced;
        for (const RungSpec &r : kRungs) {
            if (!started) {
                started = r.mode == m_fileMode;
                if (!started)
                    continue;
            } else if (forced ? r.forceOnly : (r.forceOnly || r.autoSkip)) {
                continue;
            }
            const bool gated =
                !forced && !(dev.width() > r.minW * m_detail && dev.height() > r.minH * m_detail);
            if (!gated && drawRung(r.mode))
                return;
        }
    }
    const bool labelFits =
        !hasTitle && dev.width() > kLabelW * m_detail && dev.height() > kLabelH * m_detail;
    if (labelFits)
        drawDirName(); // the cell's own name (Auto fallback, or a fileless dir)
    else if (!node->files.empty())
        drawDots(); // floor: density dots where even the name won't fit
}

void TreemapItem::drawUnscannedMark(QPainter *p, const QRectF &dev) const {
    // A sparse diagonal hatch in the text colour at low alpha over the cell body, the
    // opposite diagonal from the amber staged-move hatch so the two never read alike.
    // Device-space so the density is constant under zoom.
    const QColor lines = m_dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 28);
    p->save();
    p->setWorldMatrixEnabled(false);
    p->fillRect(dev, QBrush(lines, Qt::BDiagPattern));
    p->restore();
}

void TreemapItem::drawDiffMark(QPainter *p, const QRectF &dev, int step) const {
    // Amber — deliberately outside the depth ramps and the group hues so a staged-move
    // mark never reads as either. Drawn device-space so the hatch density and the badge
    // stay constant under zoom.
    const QColor diff(255, 165, 60);
    p->save(); // the painter is shared across the recursive drawCell walk — the bold
               // badge font (and pen/brush) must not bleed into the next cell's text
    p->setWorldMatrixEnabled(false);
    QColor lines = diff;
    lines.setAlpha(70); // sparse, low-alpha hatch so nested cells still read through it
    p->fillRect(dev, QBrush(lines, Qt::FDiagPattern));
    QPen border(diff, 2.0);
    border.setJoinStyle(Qt::MiterJoin);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRect(dev.adjusted(1.0, 1.0, -1.0, -1.0));
    // Step badge (matches the queue dock row number) in the top-right — skip on cells
    // too small to host it without burying their own content.
    if (dev.width() > 26.0 && dev.height() > 20.0) {
        QFont f = p->font();
        f.setPixelSize(10);
        f.setBold(true);
        p->setFont(f);
        const QString label = QString::number(step);
        const qreal bw = std::max(15.0, QFontMetrics(f).horizontalAdvance(label) + 8.0);
        const QRectF badge(dev.right() - bw - 2.0, dev.top() + 2.0, bw, 14.0);
        p->setPen(Qt::NoPen);
        p->setBrush(diff);
        p->drawRoundedRect(badge, 3.0, 3.0);
        p->setPen(QColor(40, 25, 0)); // dark glyph on the amber chip
        p->drawText(badge, Qt::AlignCenter, label);
    }
    p->setWorldMatrixEnabled(true);
    p->restore();
}

void TreemapItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) {
    painter->setRenderHint(QPainter::Antialiasing, false); // crisp rectangle edges
    m_dark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
    m_anyFocus = false; // focus mode dims every non-member; resolve it once per paint
    if (m_groups)
        for (const auto &g : m_groups->groups())
            if (g->view.visible && g->view.focus) {
                m_anyFocus = true;
                break;
            }
    // Staged-move diff targets (#12): identity → 1-based step for every node the active
    // plan relocated, so drawCell can mark it. Rebuilt each paint, so it tracks the
    // queue dock's scrub/undo/redo (which re-projects and repaints).
    m_diffSteps.clear();
    if (m_scene) {
        const std::vector<core::MoveOp> active = m_scene->ledger().active();
        for (int i = 0; i < static_cast<int>(active.size()); ++i)
            m_diffSteps.insert(active[i].source, i + 1); // a re-moved node shows its latest step
    }
    const QTransform toDevice = painter->worldTransform();
    const QRectF exposed = option ? option->exposedRect : QRectF(0, 0, m_w, m_h);
    // Geometry comes from the layout cache, keyed on the panel size, metric, LOD
    // factors and zoom: a pan or a plain repaint reuses it; a zoom step rebuilds.
    TreemapLayout::Params lp;
    lp.width = m_w;
    lp.height = m_h;
    lp.metric = m_metric == Bytes ? TreemapLayout::Bytes : TreemapLayout::Files;
    lp.reveal = m_reveal;
    lp.detail = m_detail;
    lp.zoom = toDevice.m11() > 0 ? toDevice.m11() : 1.0;
    lp.freezeLazy = m_interacting; // stable under the cursor; honest once idle
    m_layout.ensure(lp);
    if (!m_layout.cells().empty())
        drawCell(painter, 0, toDevice, exposed);
}

const core::FsNode *TreemapItem::cellAt(const QPointF &p) const {
    const LayoutCell *c = m_layout.cellAt(p);
    return c ? c->node : nullptr;
}

void TreemapItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    // Find the deepest hit cell *and* its rect (the rect anchors the drag arrow's tail).
    const LayoutCell *cell = m_layout.cellAt(event->pos()); // deepest cell under the press
    const core::FsNode *hit = cell ? cell->node : nullptr;
    const QRectF hitRect = cell ? cell->rect : QRectF();
    if (hit) {
        m_selected = hit; // left click selects; a movable cell also arms a drag (#10)
        update();
        // Arm the move drag only for a re-parentable node: not this surface's own root,
        // and only inside a frame (base or lens — ADR-302 #13). The drag actually begins
        // on mouseMoveEvent once the cursor passes a small threshold, so plain clicks and
        // double-clicks still behave. A drag from/to a lens resolves to the base its
        // subtree belongs to; the scene's drop legality (updateMoveDrag) checks the
        // resolved base nodes, so a node the base doesn't contain reads illegal.
        m_pressNode = nullptr;
        if (hit != m_root && hit->parent && m_ownerFrame) {
            m_pressNode = hit;
            m_pressScene = event->scenePos();
            m_pressCenterScene = mapToScene(hitRect).boundingRect().center();
            m_dragging = false;
        }
        event->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(event);
}

void TreemapItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (m_pressNode && (event->buttons() & Qt::LeftButton)) {
        constexpr int kDragThreshold = 6; // px in scene space before a press becomes a drag
        if (!m_dragging && (event->scenePos() - m_pressScene).manhattanLength() > kDragThreshold &&
            m_scene)
            m_dragging = m_scene->beginMoveDrag(m_pressNode, m_pressCenterScene);
        if (m_dragging && m_scene)
            m_scene->updateMoveDrag(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void TreemapItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (m_dragging && m_scene) {
        // Commit a legal drop. endMoveDrag captures the op and defers the re-projection
        // (which would delete this very item), so `this` stays valid through the return.
        m_scene->endMoveDrag(true);
        m_pressNode = nullptr;
        m_dragging = false;
        event->accept();
        return;
    }
    m_pressNode = nullptr; // a plain click: nothing to drop
    QGraphicsItem::mouseReleaseEvent(event);
}

void TreemapItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    // ADR-303: double-click opens a floating investigation frame on that subtree
    // (a non-destructive lens) instead of re-rooting the map. Find the deepest cell
    // under the cursor and hand the scene both the node and its rect (mapped to
    // scene space) so the frame can anchor its callout lines to the origin square.
    const LayoutCell *cell = m_layout.cellAt(event->pos()); // deepest cell under the click
    const core::FsNode *hit = cell ? cell->node : nullptr;
    const QRectF hitRect = cell ? cell->rect : QRectF();
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
