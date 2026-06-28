---
status: Accepted
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-301, ADR-302, ADR-101]
---

# ADR-303: Investigation frames: double-click opens a floating focused treemap with callout lines, one shared scene and ledger

## Context

ADR-301 made the canvas a treemap and had double-click *re-root* the view onto a subtree
(with an Up button to ascend). Re-rooting destroys context — you lose the whole-map view, and
you can only look at one subtree at a time. We want to investigate a deep subtree *without*
losing the surrounding map, to look at several subtrees at once, and to still move things
between any of them under the single move ledger (ADR-302). This ADR fixes that interaction.

## Decision

**Double-click opens a floating investigation frame; it no longer re-roots.** The frame floats
above the canvas and contains its *own* squarified treemap rooted at the double-clicked
square's subtree (semantic LOD applies within it, ADR-301). The base canvas stays at the
scanned root. This revises ADR-301's double-click, and the now-redundant Up/drill control is
removed (the base is re-rooted only by Open Folder).

- **One scene, recursive.** Frames are items in the *same* level-0 `QGraphicsScene`, not
  separate OS windows. The base is level 0; double-clicking a square inside any frame opens a
  deeper frame (level n+1). Multiple frames per level are allowed. Z-order keeps a child frame
  above its origin, with click-to-raise within a level.
- **Anchoring.** Frames live in scene space (one shared coordinate system) and carry a
  draggable header so the operator can move them off whatever they obscure; a close control
  dismisses them.
- **One shared ledger, for free.** Because everything is one scene, the move arrow
  (`✕————▶` / `✕————✕`, ADR-302) and hit-testing span the base and every frame: a move dragged
  from a square in one frame onto a square in another frame (or the base) is just another op in
  the single queue. Cross-frame overlay vectors are drawn on a top layer above all frames; hit
  testing resolves the deepest frame under the cursor.
- **Frames root on node identity, not position.** A frame therefore survives moves and
  queue-scrubs — its subtree travels with its root. Scrubbing the queue re-renders the base
  *and every open frame* to the projected state at that step (ADR-302 projection).
- **Zoom-into visual language.** A frame is tied to its origin square by **callout lines**
  between two diagonal corners (origin upper-right → frame upper-right, origin lower-left →
  frame lower-left), layered **above the parent square but below the child frame**, so the frame
  reads as an enlargement of that region. Frames cast an **ordered-dither drop shadow** (the
  project's future-retro-modern dither language). Shadow + callout lines together mark "this is
  a focused subset," composable to arbitrary depth.
- **Shared vector language.** Callout lines and the move arrow use the same hairline/colour
  conventions at every level.

## Consequences

### Positive

- Investigate deep subtrees without losing the whole-map context; compare several subtrees side
  by side instead of one re-rooted view at a time.
- One scene + one ledger means moves behave identically on the base, inside a frame, and across
  frames — "the ledger stays sane" by construction, not by special-casing.
- Frames rooted on identity survive moves/scrubs; the ADR-302 projection drives the base and all
  frames uniformly.
- Replaces a context-destroying re-root with a non-destructive, stackable lens.

### Negative

- Frame management (z-order, drag, resize, close, callout-line layering, dither shadow) plus
  nested frames and cross-frame hit-testing is real custom `QGraphicsItem` work.
- Many open frames can clutter — needs close/minimize and sane defaults.
- Overlay vectors and callout lines must render on a top layer above all frames while still
  anchoring to squares that may be inside other frames.

### Neutral

- Per-frame independent zoom/pan and per-frame appearance (size metric / colour ramp / Detail)
  are deferred; frames inherit the global view settings and fit their subtree.
- Removes ADR-301's double-click re-root and the Up control (a revision noted in ADR-301).

## Alternatives Considered

- **Re-root on double-click (ADR-301 original).** Rejected: destroys context and limits you to
  one investigation at a time.
- **Separate top-level OS windows per investigation.** Rejected: breaks the single scene and
  coordinate space, which is exactly what keeps the cross-frame arrows and the shared move
  ledger sane.
- **Expand-in-place (grow the square within the map).** Rejected: reshuffles the surrounding map
  — the very thing the treemap UX avoids — and can't show multiple foci at once.
