// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Squarified treemap layout (Bruls/Huizing/van Wijk). Pure geometry, no painter or
// scene state — split out of TreemapItem so the layout algorithm can be read, tested
// and reused independently of the rendering that consumes it.
#pragma once

#include <QRectF>

#include <vector>

namespace ui {

// Lay `weights` into `bounds` with each rect's area ∝ its weight and aspect ratios
// kept as near square as the greedy row-packing allows. Rects are returned in input
// order (out[i] is weights[i]'s cell). A zero/empty/degenerate input (no weight, or a
// non-positive bound) yields default rects so callers can paint nothing safely.
std::vector<QRectF> squarify(const std::vector<double> &weights, const QRectF &bounds);

} // namespace ui
