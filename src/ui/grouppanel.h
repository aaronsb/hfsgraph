// Left dock panel for the canvas's surfaces and semantic groups. At the top, a
// list of the open *base* surfaces (ADR-304), each removable. Below, a column of
// collapsible ("window-shade") group cards (ADR-102), one per group, that serve as
// legend and control surface: each shows the group's colour swatch, name, and
// member count, and toggles its view state (show / highlight / focus / dim) which
// drives the treemap overlay (ADR-301). A depth-ramp legend sits at the bottom. The
// panel reads and mutates GraphScene's surfaces + GroupStore and asks it to repaint
// after each change; it refreshes itself on GraphScene::surfacesChanged.
#pragma once

#include <QWidget>

class QLabel;
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

    // Rebuild the cards from the scene's GroupStore and redraw the depth legend.
    // Call after a (re)scan (groups re-resolved) or a colour-ramp change.
    void refresh();

  private:
    void rebuildBases(); // the removable base-surface rows (ADR-304)
    void rebuildCards();
    QWidget *makeCard(core::Group *group);

    GraphScene *m_scene = nullptr;
    QVBoxLayout *m_basesLayout = nullptr;  // holds the base rows (no trailing stretch)
    QVBoxLayout *m_cardsLayout = nullptr;  // holds the cards + a trailing stretch
    QLabel *m_legend = nullptr;            // depth-ramp legend strip
};

} // namespace ui
