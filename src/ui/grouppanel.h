// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Left dock panel for the canvas's surfaces and semantic groups. At the top, a
// list of the open *base* surfaces (ADR-304), each removable. Below, a **table** of
// the semantic groups (ADR-102) — one row per group with a colour swatch, name,
// member count, and the four view-state toggles (Show / Hi / Focus / Dim) as
// checkable columns driving the treemap overlay (ADR-301). Rows are multi-selectable
// and a bulk-action bar applies a state to all selected groups at once (the
// git-worktree rule can resolve many groups at depth, so per-row clicking does not
// scale). A depth-ramp legend sits at the bottom. The panel reads and mutates
// GraphScene's surfaces + GroupStore and asks it to repaint after each change; it
// refreshes itself on GraphScene::surfacesChanged.
#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;

namespace core {
struct Group;
}

namespace ui {

class GraphScene;

class GroupPanel : public QWidget {
    Q_OBJECT
  public:
    explicit GroupPanel(GraphScene *scene, QWidget *parent = nullptr);

    // Rebuild the bases list + group table from the scene's state and redraw the
    // depth legend. Call after a (re)scan, a surface add/remove, or a ramp change.
    void refresh();

  private:
    void rebuildBases(); // the removable base-surface rows (ADR-304)
    void rebuildTable(); // the group rows (preserves nothing; full repopulate)

    core::Group *groupForRow(int row) const;
    void onItemChanged(QTableWidgetItem *item); // a per-row checkbox toggled

    // Bulk operations over the selected rows (or every row when nothing is selected).
    void applyToTargets(const std::function<void(core::Group &)> &fn);
    void selectAllRows(bool on);
    void invertSelection();

    GraphScene *m_scene = nullptr;
    QVBoxLayout *m_basesLayout = nullptr; // holds the base rows (no trailing stretch)
    QTableWidget *m_table = nullptr;      // one row per group
    QLabel *m_legend = nullptr;           // depth-ramp legend strip
    bool m_populating = false;            // guard so rebuilds don't fire itemChanged
};

} // namespace ui
