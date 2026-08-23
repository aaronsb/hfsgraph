// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// The treemap's geometry, separated from its painting (ADR-301). A layout is the
// list of cells — one per directory that is large enough on screen to show — with
// each cell's rect in item coordinates, its depth, and its tree links, computed by
// squarifying each subdivided directory's children into its inner rect.
//
// Built once per set of parameters (panel size, size metric, the two LOD factors,
// and the view zoom — the subdivision gate and the chrome insets are device-pixel
// constants, so geometry depends on zoom) and reused until one changes: a pan or a
// repaint walks the cached cells; a zoom step rebuilds. Hit-testing, callout
// anchoring, and the coming file-glyph hit-test all read this one structure, so
// "what is where" has a single answer. No Qt GUI dependency — unit-testable.
#pragma once

#include <QPointF>
#include <QRectF>

#include <unordered_map>
#include <vector>

namespace core {
struct FsNode;
}

namespace ui {

struct LayoutCell {
    QRectF rect; // item coordinates
    const core::FsNode *node;
    int depth;               // nesting depth under the layout root (root = 0)
    int parent;              // index of the parent cell, -1 for the root
    int firstChild = -1;     // index of the first child cell, -1 if a leaf
    int nextSibling = -1;    // index of the next sibling cell, -1 if last
    bool subdivided = false; // children were laid out (the LOD gate passed)
    // The gate passed but the node has no children *yet*: the scan stopped here
    // (truncatedDepth). The painter asks the scene to deepen it (lazy deepening).
    bool wantsChildren = false;
    bool hasTitle = false;     // wide/tall enough on screen for a header strip
    QRectF inner;              // the area children tile (rect minus header + pad)
    std::vector<QRectF> holes; // child rects culled for size: nothing paints them
};

class TreemapLayout {
  public:
    enum Metric { Files, Bytes };

    // On-screen thresholds (device px). Semantic zoom decides detail by how big a
    // cell is on screen, not by tree depth. Shared with the painter so chrome and
    // geometry agree.
    static constexpr double kMinDevPx = 3.0;  // smaller than this: not a cell at all
    static constexpr double kSubdivW = 150.0; // subdivide once this wide on screen…
    static constexpr double kSubdivH = 64.0;  // …and this tall
    static constexpr double kLabelW = 42.0;   // room for a name
    static constexpr double kHeaderPx = 16.0; // header strip atop a subdivided cell
    static constexpr double kPadPx = 2.0;     // inset around a child block

    struct Params {
        qreal width = 0, height = 0; // panel size, item units
        Metric metric = Files;
        qreal reveal = 1.0; // subdivision gate multiplier (<1 subdivides sooner)
        qreal detail = 1.0; // title gate multiplier
        qreal zoom = 1.0;   // device px per item unit; one scale for both axes (the
                            // view never scales anisotropically)
        bool operator==(const Params &o) const; // exact — params are deterministic copies
    };

    // Point the layout at a tree. Drops the weight memo and the cells.
    void setRoot(const core::FsNode *root);
    const core::FsNode *root() const { return m_root; }

    // Build the cells for `p` unless the current cells already match. Returns true
    // when a rebuild happened.
    bool ensure(const Params &p);

    // Forget the cells and weights (the tree changed underneath — a deepened
    // subtree, a re-projection). The next ensure() rebuilds. Weights are memoized by
    // node pointer, so an in-place tree mutation must invalidate every layout whose
    // root is an ancestor of the changed node — lenses over the subtree included.
    void invalidate();

    const std::vector<LayoutCell> &cells() const { return m_cells; }
    const Params &params() const { return m_params; }

    // The deepest cell containing an item-space point, or null.
    const LayoutCell *cellAt(const QPointF &p) const;

    // The cell for a node, or null when the node has no cell (not under the root,
    // or culled for size at the current zoom).
    const LayoutCell *cellFor(const core::FsNode *node) const;

    // The rect a node occupies, whether or not it currently has a cell: the cached
    // rect when it does, else replayed top-down from the root at the current
    // params — so a callout can anchor to a square too small to draw.
    QRectF rectFor(const core::FsNode *node) const;

    // Subtree weight under the current metric (files or bytes), memoized. Floored
    // at 1 so empty directories still get a sliver.
    // A lazily-deepened node keeps the weight it had when scanned (its own files
    // only), so deepening never re-flows the map around it; its children divide
    // that fixed area among themselves.
    double weight(const core::FsNode *n) const;

  private:
    // Lay out `node` in `rect` as a child of cell `parentIndex` (-1 = root), linking
    // it after `prevSibling` (-1 = first). Returns the new cell's index, or -1 when
    // the node is too small on screen to be a cell.
    int build(int parentIndex, int prevSibling, const core::FsNode *node, const QRectF &rect,
              int depth);
    // Sorted children (heaviest first) with their weights — the squarify input.
    void childOrder(const core::FsNode *node, std::vector<const core::FsNode *> &kids,
                    std::vector<double> &weights) const;
    QRectF innerRect(const QRectF &rect, bool hasTitle) const;

    const core::FsNode *m_root = nullptr;
    Params m_params;
    bool m_valid = false;
    std::vector<LayoutCell> m_cells;
    std::unordered_map<const core::FsNode *, int> m_index; // node → cell index
    mutable std::unordered_map<const core::FsNode *, double> m_weight;
};

} // namespace ui
