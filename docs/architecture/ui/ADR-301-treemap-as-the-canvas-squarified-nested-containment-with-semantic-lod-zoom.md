---
status: Accepted
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-300, ADR-303, ADR-101, ADR-200]
---

# ADR-301: Treemap as the canvas: squarified nested-containment with semantic LOD zoom

## Context

ADR-300 fixed the canvas as a *continuum*: containment drawn either as node-link edges
(force-directed look) or as nested boxes (treemap look), morphing between the two on
collapse/expand, with a "snap-to-physics" layout to fight hairballs. We built that end to
end — a d3-style force simulation, then a Reingold–Tilford tidy-tree as a cleaner node-link
layout — and learned three things by using it on real trees:

1. **The wire is redundant with the box.** The structural graph is a strict containment tree
   (ADR-101): every node has exactly one parent. In a filesystem, "A's box is inside B's
   box" and "an edge runs A→B" encode the *same fact*. Drawing both is double work, and the
   edges are the part that turns into spaghetti.
2. **Node-link layout is O(N) in one dimension.** A wide, shallow tree (e.g. `~/Projects` at
   depth 2 — hundreds of sibling leaves) becomes an unusable ~30,000px tower no matter how
   tidy the layout; the only good property left is a shared trunk, which collapses the moment
   "siblings" means hundreds.
3. **The treemap end already does everything we wanted.** It encodes containment spatially
   with no edges to route, packs high-fan-out directories compactly, and makes "what
   dominates" obvious at a glance. It *is* ADR-300's nested-containment morph — just made
   primary instead of a state you toggle into.

The continuum's node-link half never earned its complexity once the treemap existed.

## Decision

**The canvas is a single squarified treemap. The node-link/force model is retired.** This
supersedes ADR-300's visualization and layout decisions (the collapse/expand edge↔nesting
morph and snap-to-physics); the design-language and two-coordinate-system parts carry
forward (see Neutral).

- **Squarified nested rectangles (Bruls/Huizing/van Wijk).** Every directory is a rectangle
  subdivided among its children; nesting *is* the parent→child relationship, so there are
  no edges. Aspect ratios are kept near 1 so cells stay readable.
- **Area encodes a selectable weight.** A cell's area is proportional to a subtree metric —
  **file count** (structure density) or **bytes on disk** (classic disk-usage map) — chosen
  in the toolbar. Empty directories get a floor so they remain visible.
- **Semantic (level-of-detail) zoom replaces both the collapse-morph and snap-to-physics.**
  Subdivision depth follows a cell's *on-screen* size, not a fixed tree depth: the recursion
  lives in `paint()`, driven by the painter zoom and the exposed viewport. A cell subdivides
  into children only once it is large enough on screen; off-screen cells are culled; labels
  and file icons render in device space at a **constant screen size** (zoom reveals depth,
  it does not enlarge pixels). A **Detail ("view distance") control** scales every LOD gate
  live (paint-only, no rebuild) so the operator tunes how far out contents populate.
- **Colour is data-viz, not decoration.** Perceptually-uniform ramps (Viridis/Magma/Plasma/
  Cividis/Turbo) plus a categorical Spectrum, mapped by nesting depth. Each cell is a
  saturated **title bar** over a value-shifted **contents area** (darker in dark mode,
  lighter in light) so file icons read against it.
- **Interaction.** Middle-button drag pans (the map fills the viewport); left-click selects;
  double-click drills in and an **Up** button ascends (re-rooting via parent pointers).
  *(Revised by [ADR-303](ADR-303-investigation-frames-double-click-opens-a-floating-focused-treemap-with-callout-lines-one-shared-scene-and-ledger.md):
  double-click now opens a floating investigation frame instead of re-rooting, and the Up/drill
  control is removed.)*
  ADR-300's *drag-to-rewire* — a drag that drops onto another node and emits a `move`
  (ADR-200) — is retained as the planned editing gesture, now literally "drag a square (and
  its contents) into another square."

## Consequences

### Positive

- Scales to wide/shallow and deep trees alike: no hairball, no O(N) tower, no edges to route.
- Far fewer moving parts. The force simulation, the tidy-tree layout, the layout modes, and
  edge routing are gone; `GraphScene` shrinks to "own the root + treemap + drill nav."
- Semantic zoom removes the fixed render-depth limit *for what's scanned*, and the Detail
  slider is a cheap, live knob (paint-only) rather than a relayout.
- One spatial language top to bottom: a directory's rectangle holds its child rectangles and
  (at the leaf) its files as icons — unifying what used to be a separate per-card file grid.

### Negative

- A treemap does not show explicit edges or name-ordered siblings: position is area-driven,
  so "where is child X" is found by scanning/zooming, not by following a line. (Search/select
  mitigates this later.)
- Very small leaves are invisible until you zoom — acceptable for a level-of-detail map, but
  it means the view is never a complete enumeration at one zoom.
- Truly unbounded depth still requires lazy scanning (scan a subtree on first zoom-in);
  until that lands, depth is bounded by the initial scan.
- Custom `paint()`-time recursion (LOD + culling + device-space text) is bespoke drawing, as
  ADR-300 already accepted for the canvas.

### Neutral

- **Carried forward from ADR-300:** the two-coordinate-system model (layout position is view
  state; structural position changes only via a `move`, ADR-200) and the drag-to-arrange vs.
  drag-to-rewire distinction; the design language (canvas themes, native KF6 widgets for
  chrome, rectangles-carry-meaning, minimal chrome). What is dropped is specifically the
  node-link depiction, the collapse/expand morph, and snap-to-physics.
- `NodeItem` (the node-link card with its file viewer) is parked, not deleted — a candidate
  for a click-to-inspect detail panel (its detail list = name/size/type/date).
- The richer many-to-many **associations overlay** (ADR-101) now has a clean backbone to ride
  on: association edges become a distinct layer drawn over the containment treemap.

## Alternatives Considered

- **Keep the tidy-tree node-link layout.** Rejected: deterministic and crossing-free, but
  still O(N) in one axis — a real filesystem's fan-out makes it a tower.
- **Keep ADR-300's morph continuum (node-link ⇄ treemap).** Rejected: maintaining the
  node-link end, the morph animation, and snap-to-physics buys nothing once the treemap is
  primary and semantic zoom handles scale.
- **Collapse/expand to manage scale** (ADR-300's mechanism). Rejected as the *primary* scale
  tool in favour of semantic zoom, which needs no manual per-node bookkeeping; explicit
  collapse may return as an override.
- **Icicle / sunburst.** Rejected: icicle has the same one-axis growth as node-link;
  sunburst wastes area on high-fan-out trees and is harder to label. Squarified treemaps use
  the plane most efficiently for this data.
