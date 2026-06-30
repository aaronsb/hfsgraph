// Bottom dock for the staged move plan (ADR-302 #11): the ledger made visible and
// editable. One row per queued MoveOp ("source → destination"), preceded by a Base
// row for the un-staged state. Selecting a row *scrubs* the projection to that step
// (ops before it applied, ops after it shown pending/dimmed); Undo/Redo walk the tail;
// Clear drops the plan; Commit is a stub until the apply engine (ADR-200) lands. The
// panel reads GraphScene's ledger and drives it through the scene's scrub/undo/redo
// API; it refreshes on GraphScene::ledgerChanged (and surfacesChanged, since removing
// a base can turn ops into no-ops).
#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace ui {

class GraphScene;

class QueuePanel : public QWidget {
    Q_OBJECT
  public:
    explicit QueuePanel(GraphScene *scene, QWidget *parent = nullptr);

    // Rebuild the op list + buttons + status from the scene's ledger. Call after any
    // ledger change (wired to GraphScene::ledgerChanged / surfacesChanged).
    void refresh();

  private:
    void onRowActivated(int row); // a row clicked → scrub the projection to that step
    void showVerifyReport();      // dry-run the plan against disk and report (ADR-200 #16a)

    GraphScene *m_scene = nullptr;
    QListWidget *m_list = nullptr;   // row 0 = Base (step 0); rows 1..n = ops
    QPushButton *m_undo = nullptr;
    QPushButton *m_redo = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_verify = nullptr;
    QPushButton *m_commit = nullptr;
    QLabel *m_status = nullptr;      // "Previewing k of n staged moves"
    bool m_populating = false;       // guard so refresh()'s selection doesn't re-scrub
};

} // namespace ui
