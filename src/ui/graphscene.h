// Hosts the treemap surfaces (ADR-301/304). The graph is a strict containment tree
// (ADR-101), rendered as squarified treemaps where nesting *is* the parent→child
// relationship and semantic (level-of-detail) zoom reveals depth — no fixed render
// depth. Every surface is a FrameItem (ADR-304): level-0 *base* frames (one per
// scanned tree, several may coexist) and the level-1+ investigation *lenses*
// (ADR-303) opened over them. GraphScene owns the semantic groups and tracks all
// frames; each FrameItem owns its own scanned subtree and its interior TreemapItem.
#pragma once

#include <QGraphicsScene>

#include "core/commit.h"
#include "core/group.h"
#include "core/move.h"

#include <QHash>

#include <memory>
#include <vector>

namespace core {
struct FsNode;
}

namespace ui {

class TreemapItem;
class FrameItem;
class MoveDragOverlay;

class GraphScene : public QGraphicsScene {
    Q_OBJECT
  public:
    explicit GraphScene(QObject *parent = nullptr);
    ~GraphScene() override; // out-of-line so the unique_ptr<FsNode> projection can free it

    // Base surfaces (ADR-304). Each base is a level-0 root FrameItem rendering its
    // own scanned tree; several may coexist (e.g. two volumes). addBase takes
    // ownership of the scanned tree (the frame holds it, RAII), floats a root frame
    // for it, and re-resolves rule groups across all bases. removeBase tears one
    // down (cascade-closing its lenses); clearBases drops them all.
    FrameItem *addBase(std::unique_ptr<core::FsNode> tree);
    void removeBase(FrameItem *base);
    void clearBases();
    bool hasBases() const { return !baseFrames().empty(); }
    std::vector<FrameItem *> baseFrames() const; // the level-0 frames, for the dock list

    // Treemap appearance (indices match TreemapItem::SizeMetric / ::Ramp). Each
    // rebuilds every surface's interior in place (frames survive).
    void setSizeMetric(int metric); // 0 = file count, 1 = bytes
    void setColorRamp(int ramp);    // Viridis/Magma/Plasma/Cividis/Turbo/Spectrum
    void setReveal(double factor);  // subdivision/nesting LOD (live; no rebuild)
    void setDetail(double factor);  // contents-crossover LOD (live; no rebuild)
    void setFileMode(int mode);     // force file rung (TreemapItem::FileMode) or Auto

    int sizeMetric() const { return m_sizeMetric; } // current metric (for frames)
    double reveal() const { return m_reveal; }      // current reveal LOD (for frames)
    double detail() const { return m_detail; }      // current detail LOD (for frames)
    int fileMode() const { return m_fileMode; }     // current file rung (for frames)

    // The base scan depth (toolbar Depth). A level-N lens scans its own subtree to
    // baseDepth + N (capped), so deeper lenses reveal more detail (ADR-304).
    void setBaseDepth(int depth) { m_baseDepth = depth; }

    // Investigation frames (ADR-303), the replacement for re-root/drill. openFrame
    // floats a new frame rooted at `node`, anchored to `originSceneRect` via callout
    // lines (a non-destructive lens — the base is never re-rooted); closeFrame
    // removes it; raiseFrame brings it to the front of the frame stack.
    void openFrame(const core::FsNode *node, const QRectF &originSceneRect,
                   FrameItem *parentFrame = nullptr);
    void closeFrame(FrameItem *frame); // also closes frames opened from within it
    void raiseFrame(FrameItem *frame);          // raises the frame and its descendants
    void refreshCallouts();                     // re-anchor every callout (view change)
    void refreshCalloutsFor(FrameItem *frame);  // only the callouts a move/resize affects

    // Callout draw mode (0 = Filled frustum, 1 = Lines, 2 = Off). Toolbar-controlled.
    void setCalloutMode(int mode);
    int calloutMode() const { return m_calloutMode; }

    // Cardinality: when true (default), a node may have at most one open frame —
    // re-opening it raises the existing one instead of stacking a duplicate. A
    // future UI toggle can flip this to allow multiple frames per node.
    void setUniqueFrames(bool unique) { m_uniqueFrames = unique; }
    bool uniqueFrames() const { return m_uniqueFrames; }

    // Semantic groups (ADR-102). The store is resolved against the *scan* root on
    // setRoot and owned here; the panel reads/edits it and calls updateGroupOverlay()
    // to repaint the treemap.
    core::GroupStore &groups() { return m_groups; }
    void updateGroupOverlay(); // repaint the overlay after a group view-state change
    int colorRamp() const { return m_colorRamp; } // current ramp (for the depth legend)

    // Group-store persistence (ADR-102 #15). The store is saved to a per-workspace JSON
    // sidecar (keyed by the primary base root's path) so colours, view state, exclusions
    // and manual membership survive a restart. saveGroups() is called on a group edit and
    // at app close; loadGroups() runs once per workspace when its base is first added
    // (before rule resolution, so the rule engine reconciles persisted groups to the tree).
    void saveGroups() const;

    // Move staging (ADR-302). The ledger is the staged plan; the canvas renders the
    // *projection* — each base's scanned tree with the ledger's active ops [0, step)
    // replayed (ADR-200 idempotent replay). Mutate the ledger, then rebuildProjection()
    // re-renders every base. While the ledger is empty the projection is the identity.
    core::Ledger &ledger() { return m_ledger; }
    void rebuildProjection();

    // Verify the staged plan against current disk (ADR-200 #16a dry-run): structural
    // legality, source identity/drift, and volume boundaries, per active op. Reads disk,
    // writes nothing — the report shown before any apply (which is the separate #16b half).
    core::CommitPlan verifyLedger() const;

    // Ledger editing from the queue dock (ADR-302 #11). Each mutates the plan, re-
    // projects every base, and emits ledgerChanged() so the dock refreshes. scrubTo
    // previews the state after `step` ops (0 = the un-staged base); clearMoves drops
    // the whole plan. (Append happens via the drag gesture, not here.)
    void undoMove();         // pop the tail op onto the redo stack
    void redoMove();         // restore the last undone op
    void clearMoves();       // drop all staged ops + redo history
    void scrubTo(int step);  // preview ops [0, step); clamps to [0, size]

    // Drag-to-move gesture (ADR-302 #10, cross-frame in #13). Any surface's treemap —
    // a base or a lens — arms a drag on press; past a small threshold it calls
    // beginMoveDrag (returns false on a null source so the treemap stays inert — the
    // source's re-parentability is gated at press-arm time), updateMoveDrag tracks the
    // cursor and lights the legal/illegal drop target (on the topmost surface under it)
    // with a ✕———▶ / ✕———✕ overlay, and endMoveDrag(true) commits a legal drop: append a
    // MoveOp then *defer* the re-projection (it deletes the interior treemaps, including
    // the one whose release is on the stack). The overlay is a top-Z scene item, owned
    // here, so the arrow spans frames.
    bool beginMoveDrag(const core::FsNode *source, const QPointF &sourceCenterScene);
    void updateMoveDrag(const QPointF &cursorScene);
    void endMoveDrag(bool drop);

    // Grow each base surface (ADR-301/304) so a *typical* directory name renders
    // untruncated: scale so the median-area dir cell can fit a high-percentile name
    // length (long-name outliers still truncate, by design), bounded by a hard max.
    // Leans on the generous sceneRect for the panning room the larger map needs.
    void fitNamesToTypical();

  Q_SIGNALS:
    // Emitted when the set of base surfaces changes (add/remove/clear) so the dock's
    // bases list and group cards can refresh together.
    void surfacesChanged();
    // Emitted when the staged move plan changes (append / undo / redo / scrub / clear)
    // so the queue dock (ADR-302 #11) can re-list the ops and update its step pointer.
    void ledgerChanged();

  private:
    void resolveGroups();     // merge persisted groups + re-resolve rule groups across bases
    void updateSceneBounds(); // generous sceneRect so panning works in all directions
    void restackFrames();     // reassign z so each callout sits just under its frame
    // The deepest base-surface cell under a scene point, with the owning base frame —
    // the move-drag drop target (bases render the projection; lens targets are #13).
    std::pair<FrameItem *, const core::FsNode *> surfaceCellAt(const QPointF &scenePos) const;

    core::GroupStore m_groups;                // semantic groups (ADR-102), owned
    core::Ledger m_ledger;                     // staged move plan (ADR-302), owned
    // Projected base forest (scanned trees + active ops replayed), owned here and
    // index-aligned with baseFrames(); empty while the ledger has no active ops (the
    // bases then render their scanned sources directly).
    std::vector<std::unique_ptr<core::FsNode>> m_projection;
    // Every frame in the scene: level-0 base frames and level-1+ lenses together
    // (ADR-304 — one surface abstraction). Base frames have no parent and no callout.
    std::vector<FrameItem *> m_frames;
    int m_sizeMetric = 0;             // TreemapItem::Files
    int m_colorRamp = 0;              // TreemapItem::Viridis
    double m_reveal = 1.0;            // subdivision LOD, persists across rebuilds
    double m_detail = 1.0;            // contents-crossover LOD, persists across rebuilds
    int m_fileMode = 0;               // TreemapItem::FileMode (0 = Auto), persists
    bool m_uniqueFrames = true;       // one frame per node (ADR-304 cardinality)
    int m_baseDepth = 2;              // toolbar scan depth; lenses scan baseDepth + level
    QSet<QString> m_loadedWorkspaces; // roots whose sidecar we've loaded (load once, ADR-102 #15)
    int m_calloutMode = 0;           // 0 Filled, 1 Lines, 2 Off (ADR-304)
    // Move-drag gesture state (#10), live only between begin and end.
    MoveDragOverlay *m_dragOverlay = nullptr;   // top-Z arrow + target highlight, owned
    const core::FsNode *m_dragSource = nullptr; // node being dragged (a render node)
    QPointF m_dragSourceCenter;                  // drag-source square centre, scene coords
    const core::FsNode *m_dragTarget = nullptr; // current drop target, or null
    bool m_dragLegal = false;                    // is the current target a legal drop?
    // key → node in the base projection, built at drag start (the projection is fixed
    // for the drag's duration). Lets updateMoveDrag resolve a dragged/target node — which
    // may live in a lens's independent tree — to the base node replay will actually move,
    // so legality matches the result instead of comparing across separate trees (#13).
    QHash<core::MemberKey, const core::FsNode *> m_dragKeyIndex;
};

} // namespace ui
