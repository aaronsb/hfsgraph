// Move staging model (ADR-302): the staged changeset that sits on top of the
// immutable scanned tree(s). The only structural operation is re-parenting a node
// — a `mv` (ADR-101) — so a staged change is a MoveOp, the plan is an ordered,
// replayable Ledger, and what the canvas renders is a *projection*: a deep copy of
// the base forest with the ledger's active ops replayed (ADR-200 idempotent
// replay). Nothing here touches disk; Commit (ADR-200) is a later, separate engine.
//
// This is the model substrate (Slice 3 / task #9). The drag gesture that appends
// ops (#10), the queue dock + scrub (#11), and the diff overlay (#12) build on it.
#pragma once

#include "core/group.h" // MemberKey, keyFor

#include <QString>

#include <memory>
#include <vector>

namespace core {

struct FsNode;

// One staged structural change: re-parent `source` under `destParent`. Both are
// MemberKeys (path today, ADR-100 durable id later) captured when the op is queued,
// so replay resolves them by identity against the pristine copy — an op survives
// later ops relocating the same nodes (ADR-200 idempotent replay).
struct MoveOp {
    MemberKey source;     // the node to move
    MemberKey destParent; // the directory to move it under
    QString sourceName;   // cached display label for the queue dock (ADR-302)
};

// An ordered, replayable changeset (ADR-200/302). Editing model: append on drop,
// undo/redo pops/pushes the tail, click-a-row sets the preview step. No mid-list
// reorder (keeps op dependencies linear, per ADR-302). `step` is the scrub pointer:
// the projection applies ops [0, step). It rides at the end (all ops previewed)
// after an append/redo and clamps when ops are removed.
class Ledger {
  public:
    void append(const MoveOp &op); // queue an op; clears the redo stack, previews all
    bool undo();                   // tail op → redo stack; false if there's nothing to undo
    bool redo();                   // restore the last undone op; false if the redo stack is empty
    void clear();                  // drop all ops and redo history

    int size() const { return static_cast<int>(m_ops.size()); }
    bool empty() const { return m_ops.empty(); }
    bool canUndo() const { return !m_ops.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    int step() const { return m_step; } // ops [0, step) are in the projection
    void setStep(int k);                // scrub preview; clamps to [0, size]

    const std::vector<MoveOp> &ops() const { return m_ops; }
    std::vector<MoveOp> active() const; // ops[0, step) — what the projection replays

  private:
    std::vector<MoveOp> m_ops;  // the queued plan, in order
    std::vector<MoveOp> m_redo; // undone ops, for redo (tail-only)
    int m_step = 0;             // scrub pointer; ops [0, m_step) applied
};

// Replay `ops` over a deep copy of `roots` (ADR-200 idempotent replay). Each op
// re-parents the matching node under the matching destination. The originals are
// untouched (the scanned trees are immutable); the returned forest is owned by the
// caller and index-aligned with `roots` (a move may relocate a node *between* roots,
// but never adds or removes a root — a null root maps to a null projection slot).
// Ops that don't resolve, that target a root, that would form a cycle (dest is the
// source or a descendant of it), or that would collide with an existing name at the
// destination are skipped — replay never corrupts the tree. (Full legality reporting
// at drop is #10; this is the safe-replay floor.)
std::vector<std::unique_ptr<FsNode>> projectForest(const std::vector<const FsNode *> &roots,
                                                   const std::vector<MoveOp> &ops);

} // namespace core
