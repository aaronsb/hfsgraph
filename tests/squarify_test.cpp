// Unit tests for the squarified-treemap layout (ui::squarify), extracted from
// TreemapItem so the algorithm can be checked in isolation. Pure geometry — a plain
// assert harness registered with ctest, matching tests/move_test.cpp's style.

#include "ui/squarify.h"

#include <QRectF>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool nearly(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

} // namespace

int main() {
    const QRectF bounds(0, 0, 100, 50); // area 5000

    // Degenerate inputs return one default rect per weight and never crash.
    {
        const auto empty = ui::squarify({}, bounds);
        check(empty.empty(), "empty weights -> empty result");

        const auto zero = ui::squarify({0.0, 0.0}, bounds);
        check(zero.size() == 2, "zero weights -> one rect each");
        check(zero[0].isNull() && zero[1].isNull(), "zero total -> default (null) rects");

        const auto degen = ui::squarify({1.0, 2.0}, QRectF(0, 0, 0, 50));
        check(degen.size() == 2 && degen[0].isNull(), "degenerate bounds -> default rects");
    }

    // Area is conserved (∝ weight) and every cell stays inside the bounds.
    {
        const std::vector<double> w{6, 3, 2, 1}; // total 12
        const auto rects = ui::squarify(w, bounds);
        check(rects.size() == w.size(), "one rect per weight");

        double total = 0.0;
        for (double x : w)
            total += x;
        double sumArea = 0.0;
        const double boundsArea = bounds.width() * bounds.height();
        for (size_t i = 0; i < rects.size(); ++i) {
            const QRectF &r = rects[i];
            sumArea += r.width() * r.height();
            // area ∝ weight: each cell is weight/total of the whole.
            check(nearly(r.width() * r.height(), boundsArea * w[i] / total, 1e-3),
                  "cell area proportional to weight");
            // contained within bounds (allow a hair of FP slack).
            check(r.left() >= bounds.left() - 1e-6 && r.top() >= bounds.top() - 1e-6 &&
                      r.right() <= bounds.right() + 1e-6 && r.bottom() <= bounds.bottom() + 1e-6,
                  "cell within bounds");
        }
        check(nearly(sumArea, boundsArea, 1e-3), "cells tile the whole area");
    }

    // A single weight fills the bounds exactly.
    {
        const auto one = ui::squarify({1.0}, bounds);
        check(one.size() == 1 && nearly(one[0].width(), 100.0) && nearly(one[0].height(), 50.0),
              "single weight fills bounds");
    }

    if (g_failures == 0)
        std::fprintf(stderr, "squarify_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
