// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treemaplayout.h"

#include "core/fsnode.h"
#include "squarify.h"

#include <algorithm>
#include <cmath>

namespace ui {

bool TreemapLayout::Params::operator==(const Params &o) const {
    return width == o.width && height == o.height && metric == o.metric && reveal == o.reveal &&
           detail == o.detail && zoom == o.zoom && freezeLazy == o.freezeLazy && focus == o.focus &&
           focusRect == o.focusRect;
}

void TreemapLayout::setRoot(const core::FsNode *root) {
    m_root = root;
    invalidate();
}

void TreemapLayout::invalidate() {
    m_valid = false;
    m_cells.clear();
    m_index.clear();
    m_weight.clear();
    m_overlayRoot = -1;
}

double TreemapLayout::weight(const core::FsNode *n) const {
    const auto it = m_weight.find(n);
    if (it != m_weight.end())
        return it->second;
    double w = m_params.metric == Bytes ? static_cast<double>(n->sizeBytes) : n->fileCount;
    if (!(n->lazyChildren && m_params.freezeLazy)) // frozen mid-gesture only
        for (const auto &c : n->children)
            w += weight(c.get());
    w = std::max(w, 1.0);
    m_weight[n] = w;
    return w;
}

void TreemapLayout::childOrder(const core::FsNode *node, std::vector<const core::FsNode *> &kids,
                               std::vector<double> &weights) const {
    kids.clear();
    weights.clear();
    kids.reserve(node->children.size());
    for (const auto &c : node->children)
        kids.push_back(c.get());
    // Stable, so equal-weight siblings keep scan order and build/rectFor agree.
    std::stable_sort(
        kids.begin(), kids.end(),
        [this](const core::FsNode *a, const core::FsNode *b) { return weight(a) > weight(b); });
    weights.reserve(kids.size());
    for (const auto *k : kids)
        weights.push_back(weight(k));
}

QRectF TreemapLayout::innerRect(const QRectF &rect, bool hasTitle) const {
    const qreal hdr = (hasTitle ? kHeaderPx : kPadPx) / m_params.zoom;
    const qreal pad = kPadPx / m_params.zoom;
    return rect.adjusted(pad, hdr, -pad, -pad);
}

QRectF TreemapLayout::frameInner(const QRectF &rect) const {
    const qreal hdr = kHeaderPx / m_params.zoom, rim = kStripPx / m_params.zoom;
    return rect.adjusted(rim, hdr, -rim, -rim);
}

std::vector<const core::FsNode *> TreemapLayout::focusChain(const core::FsNode *focus) const {
    std::vector<const core::FsNode *> chain;
    if (!focus || focus == m_root)
        return chain;
    for (const core::FsNode *n = focus->parent; n; n = n->parent) {
        chain.push_back(n);
        if (n == m_root) {
            std::reverse(chain.begin(), chain.end());
            return chain;
        }
    }
    chain.clear(); // never reached the root: not under it
    return chain;
}

const LayoutCell *TreemapLayout::focusCell() const {
    if (m_overlayRoot < 0)
        return nullptr;
    // The frames are a single chain: walk firstChild until the focus node's cell.
    const LayoutCell *c = &m_cells[static_cast<std::size_t>(m_overlayRoot)];
    while (c->focusFrame && c->firstChild >= 0)
        c = &m_cells[static_cast<std::size_t>(c->firstChild)];
    return c->focusFrame ? nullptr : c; // a frame without a focus cell: culled for size
}

bool TreemapLayout::ensure(const Params &p) {
    if (m_valid && p == m_params)
        return false;
    const bool weightsChanged = p.metric != m_params.metric || p.freezeLazy != m_params.freezeLazy;
    m_params = p;
    if (weightsChanged)
        m_weight.clear();
    m_cells.clear();
    m_index.clear();
    m_overlayRoot = -1;
    m_valid = true;
    if (!m_root || p.width <= 0 || p.height <= 0 || p.zoom <= 0)
        return true;
    build(-1, -1, m_root, QRectF(0, 0, p.width, p.height), 0);

    // Layout focus (#40): the overlay. One frame cell per ancestor, each the inner of
    // the one before, then the focus subtree squarified into the innermost frame.
    const std::vector<const core::FsNode *> chain = focusChain(p.focus);
    if (chain.empty() || !p.focusRect.isValid())
        return true;
    m_overlayRoot = static_cast<int>(m_cells.size());
    QRectF rect = p.focusRect;
    int parent = -1;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const int index = static_cast<int>(m_cells.size());
        LayoutCell frame;
        frame.rect = rect;
        frame.node = chain[i];
        frame.depth = static_cast<int>(i);
        frame.parent = parent;
        frame.subdivided = true;
        frame.hasTitle = true;
        frame.focusFrame = true;
        frame.overlay = true;
        frame.inner = frameInner(rect);
        m_cells.push_back(frame); // frames are not indexed: cellFor(ancestor) stays canonical
        if (parent >= 0)
            m_cells[static_cast<std::size_t>(parent)].firstChild = index;
        parent = index;
        rect = frame.inner;
    }
    const std::size_t first = m_cells.size();
    build(parent, -1, p.focus, rect, static_cast<int>(chain.size()));
    for (std::size_t i = first; i < m_cells.size(); ++i)
        m_cells[i].overlay = true; // the focus subtree: indexed, so cellFor(focus) = overlay
    return true;
}

QRectF TreemapLayout::focusRectAround(const core::FsNode *focus, const core::FsNode *child,
                                      const QRectF &childRect, const QRectF &viewRect) const {
    const std::vector<const core::FsNode *> chain = focusChain(focus);
    if (chain.empty() || !viewRect.isValid())
        return {};
    // Squarify is scale-free, so lay the children into a probe of the view's shape and
    // read the child's share of it as fractions.
    QRectF probe = viewRect;
    for (std::size_t i = 0; i < chain.size(); ++i)
        probe = frameInner(probe);
    const QRectF probeInner = innerRect(probe, true);
    std::vector<const core::FsNode *> kids;
    std::vector<double> ws;
    childOrder(focus, kids, ws);
    const std::vector<QRectF> rects = squarify(ws, probeInner);
    const auto it = std::find(kids.begin(), kids.end(), child);
    if (it == kids.end())
        return {};
    const QRectF r = rects[static_cast<std::size_t>(it - kids.begin())];
    if (r.width() <= 0 || r.height() <= 0)
        return {};
    // Scale the probe so the child's share has childRect's area, then place it so the
    // share's centre sits on childRect's centre; the aspect follows the view shape.
    const double s = std::sqrt(childRect.width() * childRect.height() / (r.width() * r.height()));
    const QPointF centre = childRect.center();
    const QPointF shareCentre = (r.center() - probe.topLeft()) * s;
    const QRectF focusCell(centre - shareCentre, probe.size() * s);
    // Back out to the outermost frame: the focus cell is the innermost frame's inner.
    const qreal hdr = kHeaderPx / m_params.zoom, rim = kStripPx / m_params.zoom;
    QRectF out = focusCell;
    for (std::size_t i = 0; i < chain.size(); ++i)
        out.adjust(-rim, -hdr, rim, rim);
    return out;
}

int TreemapLayout::build(int parentIndex, int prevSibling, const core::FsNode *node,
                         const QRectF &rect, int depth) {
    const double devW = rect.width() * m_params.zoom, devH = rect.height() * m_params.zoom;
    if (devW < kMinDevPx || devH < kMinDevPx)
        return -1; // too small on screen to be a cell

    const int index = static_cast<int>(m_cells.size());
    LayoutCell cell;
    cell.rect = rect;
    cell.node = node;
    cell.depth = depth;
    cell.parent = parentIndex;
    cell.hasTitle = devW > kLabelW * m_params.detail && devH > kHeaderPx * 1.5 * m_params.detail;
    const bool gate = devW > kSubdivW * m_params.reveal && devH > kSubdivH * m_params.reveal;
    // The canonical cell of the focus node stops here: its subtree lives in the overlay.
    cell.focusShadow = node == m_params.focus && m_overlayRoot < 0 && parentIndex >= 0;
    cell.subdivided = gate && !node->children.empty() && !cell.focusShadow;
    cell.wantsChildren = gate && node->children.empty() && node->truncatedDepth;
    cell.inner = innerRect(rect, cell.hasTitle);
    m_cells.push_back(cell);
    m_index[node] = index;
    // Link after the previous sibling, or as the parent's first child — O(1), and the
    // only place links are written. (Index-based: m_cells may reallocate below.)
    if (prevSibling >= 0)
        m_cells[static_cast<std::size_t>(prevSibling)].nextSibling = index;
    else if (parentIndex >= 0)
        m_cells[static_cast<std::size_t>(parentIndex)].firstChild = index;

    if (!cell.subdivided)
        return index;
    std::vector<const core::FsNode *> kids;
    std::vector<double> ws;
    childOrder(node, kids, ws);
    const std::vector<QRectF> rects = squarify(ws, cell.inner);
    int prev = -1;
    for (std::size_t k = 0; k < kids.size(); ++k) {
        const int child = build(index, prev, kids[k], rects[k], depth + 1);
        if (child >= 0)
            prev = child;
        else
            m_cells[static_cast<std::size_t>(index)].holes.push_back(rects[k]); // culled: a hole
    }
    return index;
}

const LayoutCell *TreemapLayout::cellAt(const QPointF &p) const {
    const LayoutCell *hit = nullptr;
    for (const LayoutCell &c : m_cells) // pre-order: the last match is the deepest
        if (c.rect.contains(p))
            hit = &c;
    return hit;
}

const LayoutCell *TreemapLayout::cellFor(const core::FsNode *node) const {
    const auto it = m_index.find(node);
    return it == m_index.end() ? nullptr : &m_cells[static_cast<std::size_t>(it->second)];
}

QRectF TreemapLayout::rectFor(const core::FsNode *target) const {
    if (!target || !m_root)
        return {};
    if (const LayoutCell *c = cellFor(target))
        return c->rect;
    // Not laid out (culled, or no build yet): replay the subdivision root → target.
    std::vector<const core::FsNode *> path;
    for (const core::FsNode *n = target; n; n = n->parent) {
        path.push_back(n);
        if (n == m_root)
            break;
    }
    if (path.empty() || path.back() != m_root)
        return {};
    std::reverse(path.begin(), path.end());
    QRectF rect(0, 0, m_params.width, m_params.height);
    std::vector<const core::FsNode *> kids;
    std::vector<double> ws;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        // Insets as the cached cells would have them at this zoom; a replayed
        // ancestor that does have a cell uses its real inner rect.
        QRectF inner;
        if (const LayoutCell *c = cellFor(path[i])) {
            inner = c->inner;
        } else {
            const double devW = rect.width() * m_params.zoom, devH = rect.height() * m_params.zoom;
            const bool hasTitle =
                devW > kLabelW * m_params.detail && devH > kHeaderPx * 1.5 * m_params.detail;
            inner = innerRect(rect, hasTitle);
        }
        childOrder(path[i], kids, ws);
        const std::vector<QRectF> rects = squarify(ws, inner);
        const auto it = std::find(kids.begin(), kids.end(), path[i + 1]);
        if (it == kids.end())
            return {};
        rect = rects[static_cast<std::size_t>(it - kids.begin())];
    }
    return rect;
}

} // namespace ui
