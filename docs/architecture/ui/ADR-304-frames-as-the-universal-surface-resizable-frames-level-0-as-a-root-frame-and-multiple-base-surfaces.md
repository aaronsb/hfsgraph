---
status: Accepted
date: 2026-06-28
deciders:
  - aaronsb
  - claude
related: [ADR-301, ADR-303, ADR-302, ADR-101]
---

# ADR-304: Frames as the universal surface: resizable frames, level 0 as a root frame, and multiple base surfaces

## Context

ADR-301 made the canvas a single squarified treemap that fills the viewport, and
ADR-303 added floating investigation frames (level 1..n lenses) over that base.
That left two asymmetries:

1. **The base is special.** Level 0 is a bare `TreemapItem` pinned to a fixed
   scene rectangle; levels 1..n are `FrameItem`s with a title, header, shadow, and
   their own treemap. Two code paths for the same idea.
2. **No way to trade area for legibility without zooming.** A treemap's bounds are
   fixed, so a cell's on-screen text room is governed only by view zoom. At a
   readable whole-map zoom, long names elide (`llmchat-knowledge-converter` →
   `llmc…rter`). The only recourse is the mouse wheel, which moves the *whole* view.

Unifying level 0 into the frame abstraction resolves both at once and, as a
bonus, removes the assumption that the canvas shows exactly one tree.

## Decision

**A frame is the only surface. Level 0 is just the root frame.** The base treemap
becomes a `FrameItem` (a *root* frame) sitting in the same scene as its lenses;
levels 1..n are unchanged. One class, one render path, top to bottom.

- **Resizable frames.** Every frame carries a corner resize grip. Dragging it
  changes the frame's panel size and calls a new `TreemapItem::setSize(w, h)`,
  which re-squarifies the interior against the larger bounds. Because labels are
  painted at a constant *screen* size (ADR-301/303), more cell **area** means more
  text room — so resizing a frame larger lets cells print their full names without
  the wheel. A resized lens therefore acts as a **magnifying lens** over its
  subtree; a resized root frame gives the whole map more breathing room. (Distinct
  from zoom, which scales pixels uniformly and moves the viewport.)

- **Multiple base surfaces.** Because level 0 is a frame, the scene may hold
  **several** root frames at once — e.g. two different disk volumes opened side by
  side, each its own scanned tree. This composes with the move ledger (ADR-302) for
  free: the ledger replays over *all* surfaces, so a move can cross surfaces and
  the projection considers everything on the canvas.

- **Frame cardinality.** By default a node has at most one open frame: re-opening it
  raises (and re-anchors) the existing one rather than stacking duplicates. A flag
  (`GraphScene::setUniqueFrames`) leaves room for a future toggle to allow multiples.

- **Callout as a filled "zoom-from" frustum.** Instead of two diagonal hairlines
  (which overlap into visual spaghetti when several frames are open), the callout is
  the **convex hull** of the origin square and the frame, with both rectangles
  subtracted out (so only the connecting cone is filled, not the boxes). It is
  flood-filled with the *light* variant of the ordered-dither texture (the drop
  shadow's dark variant inverted), in device space so the tile stays pixel-perfect
  while the cone's area scales — reusing one dither function for both.

- **Root vs. lens frames.** A root frame has a title header and resize grip like any
  frame, but **no callout lines** (it has no origin square) and is removable rather
  than "closed into" a parent. Lens frames keep their callout to the origin square
  and their close-cascade lineage (ADR-303). Closing a root frame removes that
  surface and any lenses opened from it.

- **Terminology.** The toolbar's "Open Folder" becomes **"Add base folder"** — on
  Linux "root" means `/`, so "base" is used throughout for a level-0 surface. Bases
  can be removed. The left dock lists the open bases (alongside the group cards),
  each removable, so the canvas's surfaces are legible and managed from one place.

## Consequences

### Positive

- One surface abstraction (root + lenses share `FrameItem`), so title/header/shadow
  /resize/overlay behave identically everywhere; less special-casing.
- Resize trades area for legibility without disturbing the viewport — the
  magnifying-lens capability the treemap lacked.
- Multiple bases turn the canvas into a workspace spanning volumes/trees, and the
  ADR-302 ledger spans them with no extra machinery.

### Negative

- `TreemapItem` must support live re-sizing (re-squarify on `setSize`); cheap
  (paint-time layout already), but a new state path.
- Rule-group resolution (ADR-102) must become multi-root aware: resolving against
  one base must not drop another base's groups. Resolution iterates all bases.
- `MainWindow` must own several scanned trees instead of one; lifetime of each base
  tree is tied to its root frame.

### Neutral

- The base no longer fills a fixed canvas rectangle; `sceneRect` grows to bound all
  surfaces. Fit-to-view fits the union of bases.
- Per-surface appearance (metric/ramp/Detail) stays global for now (as ADR-303
  deferred for lenses); per-frame overrides remain a later option.
- Revises ADR-301 (single base filling the canvas) and extends ADR-303 (frames now
  include level 0).

## Alternatives Considered

- **Keep level 0 special; add resize only to lenses.** Rejected: leaves the base
  un-resizable (the case that motivated this — the whole map's legibility) and keeps
  two code paths.
- **Resize by scaling the item transform** (instead of re-squarifying). Rejected:
  scaling stretches text and cells uniformly — that is just zoom, which does not buy
  more text room per cell. Re-squarifying into larger bounds is what adds area.
- **Multiple windows for multiple volumes.** Rejected for the same reason ADR-303
  rejected per-frame OS windows: it breaks the single scene/coordinate space that
  keeps the cross-surface move ledger sane.
- **"Add root" terminology.** Rejected: "root" collides with the filesystem root
  (`/`) on Linux; "base" is unambiguous.
