// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// A horizontal QHeaderView that paints the text of selected sections rotated -90°
// (reading bottom-to-top), so narrow checkbox columns can carry a full-word label
// instead of a single letter. Sections not marked rotated paint normally, so a wide
// name column keeps its ordinary left-aligned header. The header grows tall enough
// for the longest rotated label by swapping width/height in sectionSizeFromContents.
#pragma once

#include <QHeaderView>
#include <QSet>

namespace ui {

class RotatedHeader : public QHeaderView {
    Q_OBJECT
  public:
    explicit RotatedHeader(QWidget *parent = nullptr);

    // Mark a logical section as rotated (or not). Call before the view is shown.
    void setSectionRotated(int logicalIndex, bool rotated = true);

  protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    QSize sectionSizeFromContents(int logicalIndex) const override;

  private:
    QSet<int> m_rotated; // logical indices painted vertically
};

} // namespace ui
