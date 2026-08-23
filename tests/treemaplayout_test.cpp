// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for ui::TreemapLayout — the cached treemap geometry, separated from
// painting. Builds a small FsNode tree by hand (no disk) and checks containment,
// proportionality, LOD gating, tree links, hit-testing, and cache reuse. Plain assert
// harness registered with ctest, matching tests/squarify_test.cpp.

#include "core/fsnode.h"
#include "ui/treemaplayout.h"

#include <QPointF>
#include <QRectF>

#include <cmath>
#include <cstdio>
#include <memory>

namespace {

int g_failures = 0;

void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

core::FsNode *addDir(core::FsNode *parent, const char *name, int files) {
    auto child = std::make_unique<core::FsNode>();
    child->name = QString::fromLatin1(name);
    child->path = parent->path + QLatin1Char('/') + child->name;
    child->fileCount = files;
    child->parent = parent;
    core::FsNode *raw = child.get();
    parent->children.push_back(std::move(child));
    return raw;
}

// root: a(40 files) { a1(10), a2(30) }, b(60), c(0)
std::unique_ptr<core::FsNode> makeTree() {
    auto root = std::make_unique<core::FsNode>();
    root->name = QStringLiteral("root");
    root->path = QStringLiteral("/root");
    core::FsNode *a = addDir(root.get(), "a", 0);
    addDir(a, "a1", 10);
    addDir(a, "a2", 30);
    addDir(root.get(), "b", 60);
    addDir(root.get(), "c", 0);
    return root;
}

bool inside(const QRectF &inner, const QRectF &outer, double eps = 1e-6) {
    return inner.left() >= outer.left() - eps && inner.top() >= outer.top() - eps &&
           inner.right() <= outer.right() + eps && inner.bottom() <= outer.bottom() + eps;
}

} // namespace

int main() {
    using ui::LayoutCell;
    using ui::TreemapLayout;

    std::unique_ptr<core::FsNode> tree = makeTree();
    const core::FsNode *root = tree.get();
    const core::FsNode *a = root->children[0].get();
    const core::FsNode *a2 = a->children[1].get();
    const core::FsNode *b = root->children[1].get();
    const core::FsNode *c = root->children[2].get();

    TreemapLayout layout;
    layout.setRoot(root);
    TreemapLayout::Params p;
    p.width = 1000;
    p.height = 600;
    p.zoom = 1.0;

    // Weights: subtree file counts, floored at 1 for the empty dir.
    check(std::fabs(layout.weight(root) - 101.0) < 1e-9 ||
              std::fabs(layout.weight(root) - 102.0) < 1e-9,
          "root weight sums subtree files (c floors to 1)");
    check(layout.weight(c) == 1.0, "empty dir weight floors at 1");
    check(layout.weight(a) == 40.0, "a weight = a1 + a2");

    check(layout.ensure(p), "first ensure builds");
    check(!layout.ensure(p), "same params: no rebuild");
    const auto &cells = layout.cells();
    check(!cells.empty() && cells[0].node == root, "root is cell 0");
    check(cells[0].rect == QRectF(0, 0, 1000, 600), "root cell spans the panel");
    check(cells[0].subdivided, "root subdivides at this size");

    // Every non-root cell sits inside its parent's inner rect; depth = parent + 1.
    for (const LayoutCell &cell : cells) {
        if (cell.parent < 0)
            continue;
        const LayoutCell &par = cells[static_cast<std::size_t>(cell.parent)];
        check(inside(cell.rect, par.inner), "child rect inside parent's inner rect");
        check(cell.depth == par.depth + 1, "child depth = parent depth + 1");
    }

    // Children of root tile the inner rect: areas ∝ weights, heaviest first.
    const LayoutCell *ca = layout.cellFor(a);
    const LayoutCell *cb = layout.cellFor(b);
    check(ca && cb, "a and b have cells");
    if (ca && cb) {
        const double areaA = ca->rect.width() * ca->rect.height();
        const double areaB = cb->rect.width() * cb->rect.height();
        check(std::fabs(areaA / areaB - 40.0 / 60.0) < 1e-6, "area ratio a:b = 40:60");
        check(cells[0].firstChild == static_cast<int>(cb - &cells[0]),
              "heaviest child (b) is first");
    }

    // Links: walking root's child chain visits exactly its three children.
    int n = 0;
    for (int i = cells[0].firstChild; i >= 0; i = cells[static_cast<std::size_t>(i)].nextSibling)
        ++n;
    check(n == 3, "root's sibling chain has three entries");

    // Hit-test: a point in a2 resolves to the deepest cell.
    const LayoutCell *ca2 = layout.cellFor(a2);
    check(ca2 != nullptr, "a2 has a cell");
    if (ca2)
        check(layout.cellAt(ca2->rect.center()) == ca2, "cellAt picks the deepest cell");
    check(layout.cellAt(QPointF(-1, -1)) == nullptr, "cellAt outside the panel is null");

    // rectFor agrees with the cached cell, and replays for a culled node.
    if (ca2)
        check(layout.rectFor(a2) == ca2->rect, "rectFor returns the cached rect");
    TreemapLayout::Params tiny = p;
    tiny.zoom = 0.05; // 50x30 device px: nothing subdivides
    check(layout.ensure(tiny), "zoom change rebuilds");
    check(layout.cells().size() == 1, "at tiny zoom only the root is a cell");
    check(layout.cellFor(a) == nullptr, "culled node has no cell");
    const QRectF replay = layout.rectFor(a);
    check(!replay.isNull() && inside(replay, QRectF(0, 0, 1000, 600)),
          "rectFor replays a culled node");

    // Metric switch: weights recomputed (bytes are all 0 → every dir floors to 1).
    TreemapLayout::Params bytes = p;
    bytes.metric = TreemapLayout::Bytes;
    layout.ensure(bytes);
    check(layout.weight(b) == 1.0, "bytes metric re-memoizes weights");

    // invalidate forces a rebuild at unchanged params.
    layout.invalidate();
    check(layout.ensure(bytes), "invalidate → ensure rebuilds");

    if (g_failures == 0)
        std::printf("treemaplayout_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
