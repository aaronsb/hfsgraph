// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rotatedheader.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionHeader>

namespace ui {

RotatedHeader::RotatedHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent) {}

void RotatedHeader::setSectionRotated(int logicalIndex, bool rotated) {
    if (rotated)
        m_rotated.insert(logicalIndex);
    else
        m_rotated.remove(logicalIndex);
}

void RotatedHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const {
    if (!m_rotated.contains(logicalIndex)) {
        QHeaderView::paintSection(painter, rect, logicalIndex);
        return;
    }
    // Let the style paint the section background / frame with an empty label,
    // then draw the text ourselves rotated into the same rect.
    QStyleOptionHeader opt;
    initStyleOption(&opt);
    opt.rect = rect;
    opt.section = logicalIndex;
    opt.text.clear();
    opt.textAlignment = Qt::AlignCenter;
    style()->drawControl(QStyle::CE_Header, &opt, painter, this);

    const QString text = model()->headerData(logicalIndex, orientation()).toString();
    painter->save();
    painter->setPen(palette().color(QPalette::ButtonText));
    painter->setFont(font());
    // Rotate about the rect's centre so the text reads bottom-to-top, then draw
    // into a width/height-swapped rect centred on the origin.
    painter->translate(rect.center());
    painter->rotate(-90);
    const QRect swapped(-rect.height() / 2, -rect.width() / 2, rect.height(), rect.width());
    painter->drawText(swapped, Qt::AlignCenter, text);
    painter->restore();
}

QSize RotatedHeader::sectionSizeFromContents(int logicalIndex) const {
    const QSize s = QHeaderView::sectionSizeFromContents(logicalIndex);
    return m_rotated.contains(logicalIndex) ? s.transposed() : s;
}

} // namespace ui
