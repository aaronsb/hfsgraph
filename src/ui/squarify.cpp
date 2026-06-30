// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "squarify.h"

#include <algorithm>

namespace ui {

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

} // namespace ui
