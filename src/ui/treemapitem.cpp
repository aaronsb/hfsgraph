// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treemapitem.h"

#include "core/fsnode.h"
#include "core/group.h"
#include "filetypestyle.h"
#include "frameitem.h"
#include "fileactions.h"
#include "graphscene.h"
#include "selection.h"
#include "treemaplayout.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFileDevice>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QDrag>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsView>
#include <QMenu>
#include <QGraphicsSceneMouseEvent>
#include <QMimeData>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QCache>
#include <QStaticText>
#include <QStyleOptionGraphicsItem>
#include <QTransform>
#include <QVariantAnimation>
#include <QWidget>

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

// The slots [first, last) of a 1-D glyph lattice (origin, pitch, `n` slots) that
// overlap [lo, hi]: the on-screen window of a cell's grid. The grid itself is laid
// over the whole cell, so what's drawn is a stable crop of it — panning inside a
// cell reveals more of the same arrangement instead of re-packing files at the
// viewport corner — while only visible glyphs cost anything.
struct Span {
    int first, last;
};
Span visibleSlots(qreal origin, qreal pitch, qreal size, int n, qreal lo, qreal hi) {
    if (n <= 0 || pitch <= 0.0 || hi < lo)
        return {0, 0};
    const int first =
        std::clamp(static_cast<int>(std::floor((lo - origin - size) / pitch)) + 1, 0, n);
    const int last = std::clamp(static_cast<int>(std::floor((hi - origin) / pitch)) + 1, 0, n);
    return {first, std::max(first, last)};
}

// The geometry of a leaf cell's contents: which rung, and where each file's glyph
// sits — in device px relative to whatever origin `dev` was given (the real device
// rect when painting; the cell's own top-left for hit-testing, so a press can be
// resolved without knowing the view offset). One planner serves paint, the file
// hit-test, and band selection, so they can't disagree (#37).
} // namespace

struct LeafPlan {
    TreemapItem::FileMode rung = TreemapItem::Auto; // Auto = no file rung (name or nothing)
    QRectF area;                                    // content area
    int cols = 0, rows = 0, count = 0;              // grid and files shown
    qreal pitchX = 0, pitchY = 0, glyphW = 0, glyphH = 0;
    bool columnMajor = false; // List fills down columns like `ls`
    double metaW = 0;         // Details: width of the perms/size/mtime column
    QRectF glyph(int i) const {
        const int col = columnMajor ? i / rows : i % cols;
        const int row = columnMajor ? i % rows : i / cols;
        return QRectF(area.x() + col * pitchX, area.y() + row * pitchY, glyphW, glyphH);
    }
    int indexAt(const QPointF &p) const {
        if (count <= 0 || !area.contains(p))
            return -1;
        const int col = static_cast<int>((p.x() - area.x()) / pitchX);
        const int row = static_cast<int>((p.y() - area.y()) / pitchY);
        if (col < 0 || col >= cols || row < 0 || row >= rows)
            return -1;
        const int i = columnMajor ? col * rows + row : row * cols + col;
        return i < count && glyph(i).contains(p) ? i : -1;
    }
};

namespace {

QString detailMeta(const core::FileEntry &fe); // defined below; Details measures it

// Marks a drag that carries hfsgraph's own selection (alongside text/uri-list for
// other applications), so a drop onto a cell stages a move only for those.
const char kSelectionMime[] = "application/x-hfsgraph-selection";

// Lay out `node`'s files for `mode` in `area` (device px). Returns false when the rung
// can't fit — the painter's former self-guard, now shared with hit-testing.
bool planRung(TreemapItem::FileMode mode, const core::FsNode *node, const QRectF &area,
              LeafPlan &out) {
    const int nfiles = static_cast<int>(node->files.size());
    out = LeafPlan();
    out.area = area;
    out.rung = mode;
    auto grid = [&](const GlyphGrid &g) {
        if (area.width() < g.size || area.height() < g.height())
            return false;
        const GridFit fit = fitGlyphs(area, g, nfiles);
        out.cols = fit.cols;
        out.rows = fit.rows;
        out.count = fit.count;
        out.pitchX = g.pitch();
        out.pitchY = g.pitchY();
        out.glyphW = g.size;
        out.glyphH = g.height();
        return true;
    };
    switch (mode) {
    case TreemapItem::Icons:
        return grid(kIconGlyph);
    case TreemapItem::IconsNamed:
        return grid(kNamedGlyph);
    case TreemapItem::Dots:
        return grid(kPixelGlyph);
    case TreemapItem::List: {
        constexpr qreal kIcon = 13.0, kColW = 150.0;
        const qreal rowH = kNameGlyph.pitch();
        if (area.height() < rowH || area.width() < kIcon + 8.0)
            return false;
        out.cols = std::max(1, static_cast<int>(area.width() / kColW));
        out.rows = std::max(1, static_cast<int>(area.height() / rowH));
        out.count = std::min(nfiles, out.cols * out.rows);
        out.pitchX = area.width() / out.cols; // columns share the width evenly
        out.pitchY = rowH;
        out.glyphW = out.pitchX;
        out.glyphH = rowH;
        out.columnMajor = true;
        return true;
    }
    case TreemapItem::Details: {
        const qreal rowH = kNameGlyph.pitch();
        if (area.height() < rowH)
            return false;
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPixelSize(10);
        // Measure the meta column from a worst-case sample (4-digit byte size + a real
        // mtime) so it never under-measures vs. a default FileEntry.
        constexpr qreal kIcon = 12.0, kGap = 4.0, kMinName = 24.0;
        core::FileEntry sample;
        sample.sizeBytes = 1023; // widest sub-KiB form: "1023 bytes"
        sample.mtime = 1;        // force a rendered datetime, not the dashes placeholder
        out.metaW = QFontMetrics(f).horizontalAdvance(detailMeta(sample));
        if (area.width() < out.metaW + kIcon + kGap + kMinName)
            return false;
        out.cols = 1;
        out.rows = std::max(1, static_cast<int>(area.height() / rowH));
        out.count = std::min(nfiles, out.rows);
        out.pitchX = area.width();
        out.pitchY = rowH;
        out.glyphW = area.width();
        out.glyphH = rowH;
        return true;
    }
    case TreemapItem::Auto:
        break;
    }
    return false;
}

// The content area of a cell body: `dev` minus the header strip and a margin.
QRectF contentArea(const QRectF &dev, bool hasTitle) {
    return dev.adjusted(3, (hasTitle ? kHeaderPx : 2.0), -2, -2);
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

QRectF lerpRect(const QRectF &a, const QRectF &b, double t) {
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t,
                  a.width() + (b.width() - a.width()) * t,
                  a.height() + (b.height() - a.height()) * t);
}

// The affine map taking `from` onto `to` (rect → rect), for carrying a cell's inner
// rect and holes along with its blended rect.
QTransform rectMap(const QRectF &from, const QRectF &to) {
    const qreal sx = from.width() > 0 ? to.width() / from.width() : 1.0;
    const qreal sy = from.height() > 0 ? to.height() / from.height() : 1.0;
    return QTransform(sx, 0, 0, sy, to.x() - from.x() * sx, to.y() - from.y() * sy);
}

} // namespace

TreemapItem::TreemapItem(const core::FsNode *root, qreal width, qreal height, SizeMetric metric,
                         Ramp ramp, GraphScene *scene)
    : m_root(root), m_w(width), m_h(height), m_metric(metric), m_ramp(ramp), m_scene(scene) {
    m_layout.setRoot(root);
    // Without this, option->exposedRect is just boundingRect(): the exposed-rect cull
    // in drawCell never culled, and leaf contents couldn't tell what was on screen.
    // With it, exposedRect is the region *this paint call* covers — which is why
    // drawLeafContents crops a cell-anchored grid to it rather than packing from its
    // corner: a partial repaint (MinimalViewportUpdate, update(rect)) then draws the
    // same glyphs the neighbouring, unrepainted pixels already show.
    setFlag(ItemUsesExtendedStyleOption, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton); // right: context menu
    setAcceptDrops(true);                                      // file drop → staged move
}

TreemapItem::~TreemapItem() = default;

void TreemapItem::adoptFocus(const core::MemberKey &key, const QRectF &focusRect) {
    m_focus = nullptr;
    m_popped = nullptr;
    m_focusRect = QRectF();
    if (key.isEmpty() || !m_root)
        return;
    // A key is an identity, not a path (ADR-102: a stamped directory's key is its
    // UUID, and a staged move keeps the key while the path changes), so the subtree is
    // searched by key. Once per interior rebuild; O(n) is fine.
    std::vector<const core::FsNode *> stack{m_root};
    const core::FsNode *found = nullptr;
    while (!stack.empty() && !found) {
        const core::FsNode *n = stack.back();
        stack.pop_back();
        if (n != m_root && core::keyFor(*n) == key) {
            found = n;
            break;
        }
        for (const auto &c : n->children)
            stack.push_back(c.get());
    }
    if (found && found->parent && !found->children.empty()) { // a leaf never focuses
        m_focus = found;
        m_focusRect = focusRect; // null → captured from the viewport on the first paint
    }
}

QRectF TreemapItem::cellRectAt(const QPointF &itemPos) const {
    const LayoutCell *c = m_layout.cellAt(itemPos);
    return c ? c->rect : QRectF();
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

double TreemapItem::morphT() const {
    if (!m_morph || m_morph->state() != QAbstractAnimation::Running)
        return 1.0;
    return m_morph->currentValue().toDouble();
}

QRectF TreemapItem::morphFrom(const LayoutCell &cell, const MorphParent *parent) const {
    // The rect this node showed before the change, by kind first (an ancestor has a
    // canonical cell *and* a frame; the focus node a shadow and an overlay cell),
    // then the other kind — so frames grow out of their canonical cells and a popped
    // subtree settles from where the overlay showed it.
    const auto &same = cell.overlay ? m_fromOverlay : m_fromCanon;
    const auto &other = cell.overlay ? m_fromCanon : m_fromOverlay;
    if (const auto it = same.find(cell.node); it != same.end())
        return it->second;
    if (const auto it = other.find(cell.node); it != other.end())
        return it->second;
    if (parent && parent->to.isValid())
        return rectMap(parent->to, parent->from).mapRect(cell.rect); // carried with the parent
    return cell.rect;
}

void TreemapItem::drawCell(QPainter *p, int index, const QTransform &toDevice,
                           const QRectF &exposed, const MorphParent *parent) const {
    const LayoutCell &cell = m_layout.cells()[static_cast<std::size_t>(index)];
    // Mid-morph (#40) a cell is drawn at a blend of where it was and where the new
    // layout puts it; its inner rect and holes ride along under the same map.
    const double t = morphT();
    QRectF rect = cell.rect, inner = cell.inner;
    std::vector<QRectF> morphedHoles;
    MorphParent self{cell.rect, cell.rect};
    if (t < 1.0) {
        self.from = morphFrom(cell, parent);
        rect = lerpRect(self.from, cell.rect, t);
        const QTransform map = rectMap(cell.rect, rect);
        inner = map.mapRect(cell.inner);
        for (const QRectF &h : cell.holes)
            morphedHoles.push_back(map.mapRect(h));
    }
    const std::vector<QRectF> &holes = t < 1.0 ? morphedHoles : cell.holes;
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
    // The focus node's shadow (#40) carries none of the per-node decoration — group
    // tint, selection outline, diff badge — the overlay copy above it is the node's
    // one rendering; the shadow only dims with the rest of the map.
    if (m_groups && !m_groups->empty() && !cell.focusShadow) {
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
    // A focus frame (#40) is all rim: it reads as a breadcrumb in the depth colour.
    const bool rimOnly = subdivide && !ovHighlight && !ovDim;
    if (rimOnly) {
        const QColor rim = cell.focusFrame ? title : body;
        p->fillRect(QRectF(rect.left(), rect.top(), rect.width(), inner.top() - rect.top()), rim);
        p->fillRect(
            QRectF(rect.left(), inner.bottom(), rect.width(), rect.bottom() - inner.bottom()), rim);
        p->fillRect(QRectF(rect.left(), inner.top(), inner.left() - rect.left(), inner.height()),
                    rim);
        p->fillRect(
            QRectF(inner.right(), inner.top(), rect.right() - inner.right(), inner.height()), rim);
        // Children culled for size leave holes the rim fill skipped; fill just those.
        for (const QRectF &hole : holes)
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
    if (node == m_selected && !cell.focusShadow) {
        QPen sel(qApp ? qApp->palette().color(QPalette::Highlight) : QColor(120, 170, 255), 2.0);
        sel.setCosmetic(true);
        p->setPen(sel);
        p->drawRect(rect);
    }

    if (subdivide) {
        dimScrim(); // dim this cell's chrome; children overdraw the inner and self-dim
        for (int c = cell.firstChild; c >= 0;
             c = m_layout.cells()[static_cast<std::size_t>(c)].nextSibling)
            drawCell(p, c, toDevice, exposed, &self);
        if (const int step = diffStepFor(node))
            drawDiffMark(p, dev, step); // on top of children, so a moved dir stays marked
        return;
    }

    if (cell.focusShadow) {
        // The focus node's canonical cell under the overlay (ADR-305): a flat cell
        // that shows its name — in the title strip when it has one, else as the leaf
        // floor (a centred name, or dots). Never its files: those live in the
        // overlay, as does any request to deepen.
        if (!hasTitle)
            drawLeafContents(p, node, dev, hasTitle, body, toDevice.mapRect(exposed));
        dimScrim();
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

// Choose the rung for a leaf cell (Auto: the richest whose gates and geometry fit;
// forced: from the requested rung, falling down the table until one fits) and lay it
// out. Returns a plan with rung == Auto when no file rung applies.
LeafPlan TreemapItem::planLeaf(const core::FsNode *node, const QRectF &dev, bool hasTitle) const {
    LeafPlan plan;
    plan.area = contentArea(dev, hasTitle);
    if (node->files.empty())
        return plan;
    const bool forced = m_fileMode != Auto;
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
        if (gated)
            continue;
        LeafPlan candidate;
        if (planRung(r.mode, node, plan.area, candidate))
            return candidate;
    }
    // Floor: density dots where even the name won't fit (Auto never picks Dots above).
    const bool labelFits =
        !hasTitle && dev.width() > kLabelW * m_detail && dev.height() > kLabelH * m_detail;
    LeafPlan dots;
    if (!labelFits && planRung(Dots, node, plan.area, dots))
        return dots;
    return plan;
}

// The leaf rung: a cell's files drawn per the plan from planLeaf — details rows,
// list (icon+name), icon+name tiles, bare icons, or pixel-dots — or the cell's own
// name when no rung applies. All rungs colour-match the file type (fileTypeColor /
// fileTypePixmap); a selected file gets a highlight behind its glyph (#37). The
// grid is laid over the whole cell body and only the on-screen window of it is drawn.
void TreemapItem::drawLeafContents(QPainter *p, const core::FsNode *node, const QRectF &dev,
                                   bool hasTitle, const QColor &body,
                                   const QRectF &visibleDev) const {
    const LeafPlan plan = planLeaf(node, dev, hasTitle);
    const QRectF &area = plan.area;
    const QRectF onScreen = area.intersected(visibleDev);

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
    if (plan.rung == Auto) {
        const bool labelFits =
            !hasTitle && dev.width() > kLabelW * m_detail && dev.height() > kLabelH * m_detail;
        if (labelFits)
            drawDirName(); // the cell's own name (Auto fallback, or a fileless dir)
        return;
    }
    if (!onScreen.isValid())
        return; // no part of the body is on screen: chrome is drawn, contents cost nothing

    // Selected names in this directory, if any — one lookup per cell.
    const QSet<QString> *selected = m_scene && !m_scene->selection().empty()
                                        ? m_scene->selection().namesIn(core::keyFor(*node))
                                        : nullptr;
    QColor hl = qApp ? qApp->palette().color(QPalette::Highlight) : QColor(120, 170, 255);
    auto highlight = [&](const QRectF &g, int pad) {
        QColor fill = hl;
        fill.setAlpha(plan.rung == Dots ? 255 : 110);
        p->fillRect(g.adjusted(-pad, -pad, pad, pad), fill);
    };

    // Visit the on-screen glyphs of the plan's grid.
    const Span rs = visibleSlots(area.y(), plan.pitchY, plan.glyphH, plan.rows, onScreen.top(),
                                 onScreen.bottom());
    const Span cs = visibleSlots(area.x(), plan.pitchX, plan.glyphW, plan.cols, onScreen.left(),
                                 onScreen.right());
    auto forVisible = [&](auto &&draw) {
        for (int r = rs.first; r < rs.last; ++r)
            for (int c = cs.first; c < cs.last; ++c) {
                const int i = plan.columnMajor ? c * plan.rows + r : r * plan.cols + c;
                if (i < plan.count)
                    draw(i, plan.glyph(i));
            }
    };

    p->setWorldMatrixEnabled(false);
    QFont f = p->font();
    f.setPixelSize(10);
    const QFontMetrics fm(f);
    switch (plan.rung) {
    case Icons:
        forVisible([&](int i, const QRectF &g) {
            const QString &name = node->files[i].name;
            if (selected && selected->contains(name))
                highlight(g, 2);
            p->drawPixmap(g.topLeft().toPoint(), fileTypePixmap(name, static_cast<int>(g.width())));
        });
        break;
    case IconsNamed: {
        // Icon with its name elided beneath, one tile per file. The name colour-matches
        // the type, centred under the icon.
        p->setFont(f);
        const int icon = static_cast<int>(kIconGlyph.size);
        forVisible([&](int i, const QRectF &g) {
            const QString &name = node->files[i].name;
            if (selected && selected->contains(name))
                highlight(g, 1);
            p->drawPixmap(
                QPoint(static_cast<int>(g.x() + (g.width() - icon) / 2), static_cast<int>(g.y())),
                fileTypePixmap(name, icon));
            p->setPen(fileTypeColor(name));
            const QStaticText st = elidedName(f, name, g.width());
            p->drawStaticText(
                QPointF(g.x() + (g.width() - st.size().width()) / 2.0, g.y() + icon + 2.0), st);
        });
        break;
    }
    case Dots:
        forVisible([&](int i, const QRectF &g) {
            const QString &name = node->files[i].name;
            if (selected && selected->contains(name))
                highlight(g, 1); // a ring round the dot
            p->fillRect(g, fileTypeColor(name));
        });
        break;
    case List: {
        // Multi-column icon + name grid, filled column-major like `ls -a`, so a tall
        // cell uses its full width instead of one wasteful column.
        constexpr qreal kIcon = 13.0, kIconGap = 3.0;
        p->setFont(f);
        forVisible([&](int i, const QRectF &g) {
            const QString &name = node->files[i].name;
            if (selected && selected->contains(name))
                highlight(g, 0);
            p->drawPixmap(
                QPoint(static_cast<int>(g.x()), static_cast<int>(g.y() + (g.height() - kIcon) / 2)),
                fileTypePixmap(name, static_cast<int>(kIcon)));
            p->setPen(fileTypeColor(name));
            const QRectF tr(g.x() + kIcon + kIconGap, g.y(), g.width() - kIcon - kIconGap - 4.0,
                            g.height());
            const QStaticText st = elidedName(f, name, tr.width());
            p->drawStaticText(QPointF(tr.x(), tr.y() + (g.height() - st.size().height()) / 2.0),
                              st);
        });
        break;
    }
    case Details: {
        // One file per row with metadata, like `ls -l`: a muted fixed-width meta column
        // (perms · size · mtime) then the type-coloured icon + name, in a monospace font.
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(10);
        p->setFont(mono);
        QColor metaCol = textColorFor(body);
        metaCol.setAlpha(160); // de-emphasised next to the file name
        constexpr qreal kIcon = 12.0, kGap = 4.0;
        forVisible([&](int i, const QRectF &g) {
            const core::FileEntry &fe = node->files[i];
            if (selected && selected->contains(fe.name))
                highlight(g, 0);
            p->setPen(metaCol);
            p->drawText(QRectF(g.x(), g.y(), plan.metaW, g.height()),
                        Qt::AlignVCenter | Qt::AlignLeft, detailMeta(fe));
            const double nx = g.x() + plan.metaW;
            p->drawPixmap(
                QPoint(static_cast<int>(nx), static_cast<int>(g.y() + (g.height() - kIcon) / 2)),
                fileTypePixmap(fe.name, static_cast<int>(kIcon)));
            p->setPen(fileTypeColor(fe.name));
            const QRectF tr(nx + kIcon + kGap, g.y(), g.right() - (nx + kIcon + kGap), g.height());
            if (tr.width() > 8.0) {
                const QStaticText st = elidedName(mono, fe.name, tr.width());
                p->drawStaticText(QPointF(tr.x(), tr.y() + (g.height() - st.size().height()) / 2.0),
                                  st);
            }
        });
        break;
    }
    case Auto:
        break;
    }
    p->setWorldMatrixEnabled(true);
}

// Hit-testing shares planLeaf: the cell's device rect is taken with its own top-left
// as the origin, so the plan's glyph rects are comparable to (itemPos − cell) × zoom.
int TreemapItem::fileIndexIn(const LayoutCell &cell, const QPointF &itemPos) const {
    if (cell.subdivided || cell.node->files.empty())
        return -1;
    const qreal z = m_layout.params().zoom;
    const QRectF dev(0, 0, cell.rect.width() * z, cell.rect.height() * z);
    const LeafPlan plan = planLeaf(cell.node, dev, cell.hasTitle);
    if (plan.rung == Auto)
        return -1;
    return plan.indexAt((itemPos - cell.rect.topLeft()) * z);
}

std::vector<int> TreemapItem::filesIn(const core::FsNode *node, const QRectF &cellRect,
                                      bool hasTitle, const QRectF &itemRect) const {
    std::vector<int> out;
    if (!node || node->files.empty())
        return out;
    const qreal z = m_layout.params().zoom;
    const QRectF dev(0, 0, cellRect.width() * z, cellRect.height() * z);
    const LeafPlan plan = planLeaf(node, dev, hasTitle);
    if (plan.rung == Auto)
        return out;
    const QRectF band = QRectF((itemRect.topLeft() - cellRect.topLeft()) * z, itemRect.size() * z);
    for (int i = 0; i < plan.count; ++i)
        if (plan.glyph(i).intersects(band))
            out.push_back(i);
    return out;
}

std::pair<const core::FsNode *, int> TreemapItem::fileAt(const QPointF &itemPos) const {
    const LayoutCell *cell = m_layout.cellAt(itemPos);
    if (!cell)
        return {nullptr, -1};
    const int i = fileIndexIn(*cell, itemPos);
    return i >= 0 ? std::make_pair(cell->node, i) : std::make_pair(nullptr, -1);
}

void TreemapItem::drawBand(QPainter *p) const {
    if (m_band.isNull())
        return;
    QColor hl = qApp ? qApp->palette().color(QPalette::Highlight) : QColor(120, 170, 255);
    QPen pen(hl, 1.0);
    pen.setCosmetic(true);
    p->setPen(pen);
    hl.setAlpha(50);
    p->setBrush(hl);
    p->drawRect(m_band);
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

void TreemapItem::setFocus(const core::FsNode *focus, const QRectF &focusRect,
                           const TreemapLayout::Params &lp, bool animate) {
    // Snapshot where every cell is shown *now* (mid-morph included) so the next
    // morph starts from what the eye sees, then switch and rebuild.
    const double t = morphT();
    const std::vector<LayoutCell> &cells = m_layout.cells();
    std::vector<QRectF> from(cells.size()), shown(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) { // pre-order: parents resolve first
        const LayoutCell &c = cells[i];
        MorphParent parent;
        if (c.parent >= 0)
            parent = {from[static_cast<std::size_t>(c.parent)],
                      cells[static_cast<std::size_t>(c.parent)].rect};
        from[i] = t < 1.0 ? morphFrom(c, c.parent >= 0 ? &parent : nullptr) : c.rect;
        shown[i] = lerpRect(from[i], c.rect, t);
    }
    m_fromCanon.clear();
    m_fromOverlay.clear();
    for (std::size_t i = 0; i < cells.size(); ++i)
        (cells[i].overlay ? m_fromOverlay : m_fromCanon)[cells[i].node] = shown[i];
    m_focus = focus;
    m_focusRect = focusRect;
    if (!focus)
        m_popped = nullptr;
    if (m_ownerFrame)
        m_ownerFrame->setLayoutFocus(focus ? core::keyFor(*focus) : core::MemberKey(), focusRect);
    if (m_scene)
        m_scene->noteLayoutFocusChanged(); // callouts re-anchor on the next turn
    TreemapLayout::Params np = lp;
    np.focus = m_focus;
    np.focusRect = m_focusRect;
    m_layout.ensure(np);
    if (!animate) {
        m_fromCanon.clear();
        m_fromOverlay.clear();
        if (m_morph)
            m_morph->stop();
        return;
    }
    if (!m_morph) {
        m_morph = std::make_unique<QVariantAnimation>();
        m_morph->setDuration(220);
        m_morph->setStartValue(0.0);
        m_morph->setEndValue(1.0);
        m_morph->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_morph.get(), &QVariantAnimation::valueChanged, m_morph.get(),
                         [this] { update(); });
        QObject::connect(m_morph.get(), &QVariantAnimation::finished, m_morph.get(), [this] {
            m_fromCanon.clear();
            m_fromOverlay.clear();
            update();
        });
    }
    m_morph->stop();
    m_morph->start();
}

void TreemapItem::decideFocus(TreemapLayout::Params &lp, const QRectF &vpDev, const QRectF &vpItem,
                              const QTransform &toDevice) {
    const bool enabled = m_scene && m_scene->layoutFocusEnabled();
    if (!enabled) {
        if (m_focus)
            setFocus(nullptr, QRectF(), lp);
        return;
    }
    const std::vector<LayoutCell> &cells = m_layout.cells();
    if (cells.empty() || !vpDev.isValid() || !vpItem.isValid())
        return;
    // Coverage is the cell's *visible* part: what is on screen, not how big the cell
    // is — so a focus panned off-screen releases, an off-screen cell can't engage,
    // and "at most one child covers 60%" holds however large the parent has grown.
    auto covers = [&](const LayoutCell &c, double f) {
        const QSizeF s = toDevice.mapRect(c.rect).intersected(vpDev).size();
        return s.width() >= vpDev.width() * f && s.height() >= vpDev.height() * f;
    };
    // Release: the focus fell under the threshold → its parent is re-squarified into
    // the viewport in its place (ADR-305 model b: ascent mirrors descent); at the root
    // the focus clears. The popped child is remembered so the engage below doesn't
    // dive straight back into it.
    if (m_focus) {
        const LayoutCell *fc = m_layout.focusCell();
        if (!fc || fc->node != m_focus) {
            setFocus(nullptr, QRectF(), lp); // gone (tree changed, or culled)
            return;
        }
        if (!covers(*fc, kFocusRelease)) {
            const core::FsNode *parent = m_focus->parent;
            m_popped = m_focus;
            m_popZoom = lp.zoom;
            setFocus(parent && parent != m_root ? parent : nullptr, vpItem, lp);
            return;
        }
    }
    // The guard lifts when the user zooms back in, or when the popped child has left
    // the eye (under the release coverage) — so a pan away and back re-engages it.
    if (m_popped) {
        const LayoutCell *pc = m_layout.cellFor(m_popped);
        if (lp.zoom > m_popZoom || !pc || !covers(*pc, kFocusRelease))
            m_popped = nullptr;
    }
    // Engage: the deepest *subdivided* cell under the current focus (or the root) that
    // covers the viewport on both axes. At most one child can (two disjoint tiles
    // can't both span 60% of the same axis), so this is a single descent. A leaf is
    // never a focus: its files already crop to the viewport (drawLeafContents), and
    // pinning a leaf to the viewport only blows its body up under further zoom.
    const LayoutCell *start = m_focus ? m_layout.focusCell() : &cells[0];
    const LayoutCell *deepest = nullptr;
    for (const LayoutCell *c = start; c && c->subdivided;) {
        const LayoutCell *next = nullptr;
        for (int k = c->firstChild; k >= 0; k = cells[static_cast<std::size_t>(k)].nextSibling)
            if (const LayoutCell &kc = cells[static_cast<std::size_t>(k)];
                kc.subdivided && kc.node != m_popped && covers(kc, kFocusEngage)) {
                next = &kc;
                break;
            }
        if (!next)
            break;
        deepest = next;
        c = next;
    }
    if (deepest && deepest->node != m_focus && deepest->node->parent)
        setFocus(deepest->node, vpItem, lp);
}

void TreemapItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                        QWidget *widget) {
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
    // Layout focus (ADR-305): judged against the viewport — `widget` is the view's
    // viewport, so its rect is the device-space window the eye has; clipped to this
    // item so a focus never lays out past the surface. A render with no widget (an
    // image grab) keeps whatever focus the screen last decided.
    QRectF vpDev, vpItem;
    if (widget && toDevice.isInvertible()) {
        vpDev = QRectF(widget->rect());
        vpItem = toDevice.inverted().mapRect(vpDev).intersected(boundingRect());
    }
    if (m_focus && m_focusRect.isNull() && vpItem.isValid())
        m_focusRect = vpItem; // an adopted focus with no rect lands in the viewport, no morph
    lp.focus = m_focus;
    lp.focusRect = m_focusRect;
    m_layout.ensure(lp);
    if (vpDev.isValid())
        decideFocus(lp, vpDev, vpItem, toDevice);
    if (!m_layout.cells().empty())
        drawCell(painter, 0, toDevice, exposed, nullptr);
    if (m_layout.overlayRoot() >= 0)
        drawCell(painter, m_layout.overlayRoot(), toDevice, exposed, nullptr); // over the map
    drawBand(painter); // rubber band, over everything
}

const core::FsNode *TreemapItem::cellAt(const QPointF &p) const {
    const LayoutCell *c = m_layout.cellAt(p);
    return c ? c->node : nullptr;
}

void TreemapItem::endFileGesture() {
    const bool held = m_pressFileDir || m_bandNode;
    m_pressFileDir = nullptr;
    m_pressFileIndex = -1;
    m_bandNode = nullptr;
    m_band = QRectF();
    if (held && m_scene)
        m_scene->releaseGestureHold(); // grafts may land again
}

void TreemapItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore(); // right-press: the context menu (#38) — never a selection change
        return;
    }
    const LayoutCell *cell = m_layout.cellAt(event->pos()); // deepest cell under the press
    m_pressNode = nullptr;
    endFileGesture();
    if (!cell) {
        QGraphicsItem::mousePressEvent(event);
        return;
    }
    const core::FsNode *hit = cell->node;
    const Qt::KeyboardModifiers mods = event->modifiers();
    Selection *sel = m_scene ? &m_scene->selection() : nullptr;

    // A file glyph (#37): select it (plain / Ctrl toggle / Shift range) and arm a
    // drag-out; the directory gestures below don't apply. While a file gesture is in
    // flight the scene holds deepen grafts, which would rebuild the cells (and, under
    // a staged move, the items) beneath us.
    if (const int fi = fileIndexIn(*cell, event->pos()); fi >= 0 && sel) {
        const bool selected =
            sel->contains(core::keyFor(*hit), hit->files[static_cast<std::size_t>(fi)].name);
        if (mods & Qt::ShiftModifier)
            sel->rangeTo(*hit, fi);
        else if (mods & Qt::ControlModifier) {
            if (!selected)
                sel->toggle(*hit, fi); // toggling *off* waits for release: a Ctrl-drag
                                       // carries the whole selection, grabbed file included
        } else if (!selected)
            sel->set(*hit, fi); // an already-selected file stays selected (it may start a drag)
        m_pressWasSelected = selected;
        m_pressFileDir = hit;
        m_pressFileIndex = fi;
        m_pressScene = event->scenePos();
        m_scene->holdGestures();
        event->accept();
        return;
    }

    m_selected = hit; // left click selects the directory
    update();

    // The file area of a leaf, off any glyph: a rubber band (#37). Ctrl adds to the
    // selection; otherwise the press clears it. The title strip (or a cell too small
    // to have one, or one without files) stays the directory's handle for reparenting.
    const qreal z = m_layout.params().zoom;
    const bool inBody = cell->hasTitle && (event->pos().y() - cell->rect.top()) * z > kHeaderPx;
    if (!cell->subdivided && !hit->files.empty() && inBody && sel) {
        if (!(mods & Qt::ControlModifier))
            sel->clear();
        m_bandNode = hit;
        m_bandRect = cell->rect;
        m_bandHasTitle = cell->hasTitle;
        m_bandOrigin = event->pos();
        m_band = QRectF();
        m_scene->holdGestures();
        event->accept();
        return;
    }

    // Arm the move drag only for a re-parentable node: not this surface's own root,
    // and only inside a frame (base or lens — ADR-302 #13). The drag actually begins
    // on mouseMoveEvent once the cursor passes a small threshold, so plain clicks and
    // double-clicks still behave. A drag from/to a lens resolves to the base its
    // subtree belongs to; the scene's drop legality (updateMoveDrag) checks the
    // resolved base nodes, so a node the base doesn't contain reads illegal.
    if (sel && !(mods & Qt::ControlModifier))
        sel->clear(); // a plain click on directory chrome drops the file selection
    if (hit != m_root && hit->parent && m_ownerFrame) {
        m_pressNode = hit;
        m_pressScene = event->scenePos();
        m_pressCenterScene = mapToScene(cell->rect).boundingRect().center();
        m_dragging = false;
    }
    event->accept();
}

void TreemapItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    constexpr int kDragThreshold = 6; // px in scene space before a press becomes a drag
    if (!(event->buttons() & Qt::LeftButton)) {
        QGraphicsItem::mouseMoveEvent(event);
        return;
    }
    if (m_pressFileDir) {
        // Drag the selection out as file URLs (to Dolphin, a terminal, another app).
        // QDrag::exec runs its own loop; the release never reaches us, so clear first.
        if ((event->scenePos() - m_pressScene).manhattanLength() > kDragThreshold && m_scene) {
            auto *mime = new QMimeData;
            mime->setUrls(m_scene->selection().urls());
            mime->setData(QLatin1String(kSelectionMime), QByteArray("1")); // our selection
            endFileGesture();
            auto *drag = new QDrag(event->widget());
            drag->setMimeData(mime);
            m_scene->holdGestures(); // QDrag::exec is a nested loop: no rebuild under us
            drag->exec(Qt::CopyAction | Qt::LinkAction, Qt::CopyAction);
            m_scene->releaseGestureHold();
            drag->deleteLater();
        }
        event->accept();
        return;
    }
    if (m_bandNode) {
        m_band = QRectF(m_bandOrigin, event->pos()).normalized().intersected(m_bandRect);
        update();
        event->accept();
        return;
    }
    if (m_pressNode) {
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
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (m_pressFileDir) {
        // A click (no drag) resolves what the press deferred: Ctrl on a selected file
        // toggles it off; a plain click narrows the selection to the file.
        if (m_scene) {
            const Qt::KeyboardModifiers mods = event->modifiers();
            if ((mods & Qt::ControlModifier) && m_pressWasSelected)
                m_scene->selection().toggle(*m_pressFileDir, m_pressFileIndex);
            else if (!(mods & (Qt::ControlModifier | Qt::ShiftModifier)))
                m_scene->selection().set(*m_pressFileDir, m_pressFileIndex); // no-op if already
        }
        endFileGesture();
        event->accept();
        return;
    }
    if (m_bandNode) {
        if (m_scene && !m_band.isNull())
            m_scene->selection().addAll(*m_bandNode,
                                        filesIn(m_bandNode, m_bandRect, m_bandHasTitle, m_band));
        endFileGesture();
        update();
        event->accept();
        return;
    }
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

void TreemapItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    if (!m_scene)
        return;
    const LayoutCell *cell = m_layout.cellAt(event->pos());
    if (!cell) {
        event->ignore();
        return;
    }
    QWidget *host = event->widget() ? event->widget()->window() : nullptr;
    QMenu menu(host);
    Selection &sel = m_scene->selection();
    // A right-click on a glyph outside the selection makes it the selection (the
    // file-manager convention); inside it, the menu acts on the whole selection.
    if (const int fi = fileIndexIn(*cell, event->pos()); fi >= 0) {
        const core::FsNode *dir = cell->node;
        if (!sel.contains(core::keyFor(*dir), dir->files[static_cast<std::size_t>(fi)].name))
            sel.set(*dir, fi);
        m_scene->fileActions().populate(menu, host);
    } else if (!sel.empty() && !cell->subdivided && !cell->node->files.empty()) {
        m_scene->fileActions().populate(menu, host); // the selection, from empty body
    } else {
        m_scene->fileActions().populateDir(menu, cell->node, host);
    }
    event->accept();
    if (menu.isEmpty())
        return;
    // The menu runs a nested event loop; hold deepen grafts and re-projections so
    // this item (the handler on the stack) isn't deleted underneath it.
    m_scene->holdGestures();
    menu.exec(event->screenPos());
    m_scene->releaseGestureHold();
}

void TreemapItem::dragEnterEvent(QGraphicsSceneDragDropEvent *event) {
    // Only a drag of hfsgraph's own selection (marked with the private mime type); a
    // drop from another application would be an import, which the ledger doesn't
    // express, and must not be mistaken for a move of whatever happens to be selected.
    if (event->mimeData()->hasFormat(QLatin1String(kSelectionMime)) && m_scene &&
        !m_scene->selection().empty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void TreemapItem::dragMoveEvent(QGraphicsSceneDragDropEvent *event) {
    const LayoutCell *cell = m_layout.cellAt(event->pos());
    if (cell && event->mimeData()->hasFormat(QLatin1String(kSelectionMime)) && m_scene &&
        !m_scene->selection().empty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void TreemapItem::dropEvent(QGraphicsSceneDragDropEvent *event) {
    const LayoutCell *cell = m_layout.cellAt(event->pos());
    if (!cell || !m_scene || !event->mimeData()->hasFormat(QLatin1String(kSelectionMime))) {
        event->ignore();
        return;
    }
    // Only our own selection is modelled (its entries resolve to scanned nodes); a
    // URL from outside hfsgraph would be an import, which the ledger doesn't express.
    const std::vector<Selection::Entry> entries = m_scene->selection().entries();
    int staged = 0;
    for (const Selection::Entry &e : entries)
        if (m_scene->stageMoveFile(e.dir, e.name, cell->node))
            ++staged;
    if (staged > 0)
        event->acceptProposedAction();
    else
        event->ignore();
}

} // namespace ui
