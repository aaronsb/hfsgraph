---
status: Accepted
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-200, ADR-301, ADR-102, ADR-101]
---

# ADR-302: Move staging: drag-arrow gesture, queued changeset with scrub preview, atomic commit

## Context

ADR-200 fixed the engine model — changes accumulate as a staged changeset that is dry-run for
legality and committed as a transaction (snapshot + WAL + idempotent replay + rollback).
ADR-101 fixed that the only structural operation is re-parenting (a `mv`) and separated
*layout* position from *structural* position. ADR-301 made the canvas a squarified treemap.
This ADR fixes the **interaction and staging UX** that sits on top of all three: how the
operator performs moves on the treemap, plans several before anything touches disk, previews
the result, and commits.

The driving UX insight: relaying out the treemap *during* a drag would reshuffle the whole
map continuously and feel awful. So the drag must be a cheap overlay, and the map should
change only at discrete, intentional moments.

## Decision

**Drag gesture (overlay only, no live relayout).**

- Picking up a square — or a whole group (ADR-102) — starts an overlay edge: an `✕` at the
  origin (the source square stays put) and a bright hairline to a `▶` arrowhead pinned to the
  cursor. The treemap is **not** re-squarified during the drag; only the overlay is drawn.
- The square under the arrowhead is the candidate target. Legality is shown **on the line
  itself**: a legal target reads `✕————▶` (bright hairline, arrowhead) with the target hitbox
  lit; an **illegal** target (moving into one's own descendant → cycle; a name collision at the
  destination; an immutable or cross-volume target, ADR-200) recolours the line and flips the
  head to an `✕`, giving `✕————✕` — an unmistakable "won't land" signal.
- Releasing over a legal target **appends the move to the queue**, and the treemap
  re-squarifies **once** to the new projected state. An illegal release bounces (no-op).
- The treemap has no free "drag-to-arrange" (layout is computed, ADR-301), so this gesture is
  purely ADR-101's drag-to-rewire.

**Staged changeset queue (the dry-run ledger, ADR-200).**

- A bottom dock pane lists the moves as **numbered ops** (`mv` source → new parent), each with
  a legality/status marker; a group move shows as one op affecting N members.
- The canvas always shows a **projection**: the immutable scanned tree with ops `[0..k]`
  replayed (ADR-200 idempotent replay). Selecting/scrubbing a row sets `k` and re-renders, so
  cycling the list animates the plan as apparent motion of change.
- Squares touched by the plan are marked (crosshatch) with a corner tag = the queue step that
  moved them — a provenance/diff overlay.
- **Editing: append on drop; undo/redo pops/pushes the tail; click any row to preview.** No
  mid-list reorder (keeps op dependencies linear and avoids whole-plan re-validation).

**Commit (atomic transaction, ADR-200).** Legality is checked at drop; at **Commit** the whole
queue is **re-verified against current disk** to catch drift since queuing, then applied as a
single transaction — snapshot, apply in order, fsync; any failure rolls back to the snapshot.
**All-or-nothing. Only Commit touches disk.**

**Confirmation ("training wheels").** Per-operation-type confirmation, configurable, default-on
for a first run. The queue+commit is itself the primary safety net (nothing hits disk until
Commit), so per-drop confirmation guards against accidental drops, not data loss.

**Groups follow for free (ADR-102 payoff).** Membership is durable-id-keyed and rule-groups
re-resolve over the projected tree, so a move changes a node's parent but not its identity —
the JSON store needs no rewrite on move.

## Consequences

### Positive

- Makes ADR-200's propose → verify → commit directly operable and legible: plan many moves,
  see each projected state, commit once. The queue is an undo/redo buffer before commit.
- No relayout during the drag keeps the gesture smooth; the map snaps only at intentional
  commit-to-queue moments.
- Atomic commit with drift re-verify makes batch reorganization safe.
- Group membership travels automatically — the ADR-102 identity decision pays off here.

### Negative

- The projection (replay over the base tree), the diff overlay, and the arrow overlay are
  bespoke drawing; and real Commit depends on the **not-yet-built** ADR-200 engine (and
  eventually the Rust core, ADR-401), so Commit is a stub until then.
- No mid-list reorder limits planning flexibility (deferred deliberately).
- Legality/drift checking must cover cycles, name collisions, immutability, and volume
  boundaries — non-trivial, and shared with ADR-200.

### Neutral

- The staging UX (arrow, queue, projection/scrub, diff overlay, legality-at-drop) is buildable
  now on the in-memory projection, ahead of the engine; Commit wires to ADR-200 when it lands.
- Post-commit undo (beyond the pre-commit queue) is a separate concern handled by ADR-200
  snapshots.

## Alternatives Considered

- **Live relayout during the drag.** Rejected — continuous reshuffle is disorienting; the
  static-map + overlay approach is the core insight.
- **Immediate per-move commit (no queue).** Rejected — forfeits batch planning and the dry-run
  preview, and forces a verify/commit per move.
- **Full reorder/edit of the queue.** Deferred — a later op may depend on an earlier op's
  result, so every reorder would re-validate the whole plan.
- **Optimistic commit without drift re-verify.** Rejected — unsafe; files may change between
  queuing and commit.
