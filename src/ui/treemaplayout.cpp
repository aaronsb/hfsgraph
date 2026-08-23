// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treemaplayout.h"

#include "core/fsnode.h"
#include "squarify.h"

#include <algorithm>
#include <cmath>

namespace ui {

bool TreemapLayout::Params::operator==(const Params &o) const {
    return qFuzzyCompare(width, o.width) && qFuzzyCompare(height, o.height) && metric == o.metric &&
           qFuzzyCompare(reveal, o.reveal) && qFuzzyCompare(detail, o.detail) &&
           qFuzzyCompare(zoom, o.zoom);
}

void TreemapLayout::setRoot(const core::FsNode *root) {
    m_root = root;
    invalidate();
    m_weight.clear();
}

void TreemapLayout::invalidate() {
    m_valid = false;
    m_cells.clear();
    m_index.clear();
    m_weight.clear();
}

double TreemapLayout::weight(const core::FsNode *n) const {
    const auto it = m_weight.find(n);
    if (it != m_weight.end())
        return it->second;
    double w = m_params.metric == Bytes ? static_cast<double>(n->sizeBytes) : n->fileCount;
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
    std::sort(kids.begin(), kids.end(), [this](const core::FsNode *a, const core::FsNode *b) {
        return weight(a) > weight(b);
    });
    weights.reserve(kids.size());
    for (const auto *k : kids)
        weights.push_back(weight(k));
}

QRectF TreemapLayout::innerRect(const QRectF &rect, bool hasTitle) const {
    const qreal hdr = (hasTitle ? kHeaderPx : kPadPx) / m_params.zoom;
    const qreal pad = kPadPx / m_params.zoom;
    return rect.adjusted(pad, hdr, -pad, -pad);
}

bool TreemapLayout::ensure(const Params &p) {
    if (m_valid && p == m_params)
        return false;
    const bool metricChanged = p.metric != m_params.metric;
    m_params = p;
    if (metricChanged)
        m_weight.clear();
    m_cells.clear();
    m_index.clear();
    m_valid = true;
    if (!m_root || p.width <= 0 || p.height <= 0 || p.zoom <= 0)
        return true;
    build(-1, m_root, QRectF(0, 0, p.width, p.height), 0);
    return true;
}

void TreemapLayout::build(int parentIndex, const core::FsNode *node, const QRectF &rect,
                          int depth) {
    const double devW = rect.width() * m_params.zoom, devH = rect.height() * m_params.zoom;
    if (devW < kMinDevPx || devH < kMinDevPx)
        return; // too small on screen to be a cell

    const int index = static_cast<int>(m_cells.size());
    LayoutCell cell;
    cell.rect = rect;
    cell.node = node;
    cell.depth = depth;
    cell.parent = parentIndex;
    cell.hasTitle = devW > kLabelW * m_params.detail && devH > kHeaderPx * 1.5 * m_params.detail;
    cell.subdivided = !node->children.empty() && devW > kSubdivW * m_params.reveal &&
                      devH > kSubdivH * m_params.reveal;
    cell.inner = innerRect(rect, cell.hasTitle);
    m_cells.push_back(cell);
    m_index[node] = index;

    // Link into the parent's child chain (appending keeps heaviest-first order).
    if (parentIndex >= 0) {
        LayoutCell &parent = m_cells[parentIndex];
        if (parent.firstChild < 0) {
            parent.firstChild = index;
        } else {
            int last = parent.firstChild;
            while (m_cells[last].nextSibling >= 0)
                last = m_cells[last].nextSibling;
            m_cells[last].nextSibling = index;
        }
    }

    if (!cell.subdivided)
        return;
    std::vector<const core::FsNode *> kids;
    std::vector<double> ws;
    childOrder(node, kids, ws);
    const std::vector<QRectF> rects = squarify(ws, m_cells[index].inner);
    for (std::size_t k = 0; k < kids.size(); ++k)
        build(index, kids[k], rects[k], depth + 1); // may push: re-index, don't hold refs
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
