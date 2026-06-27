---
status: Draft
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-101, ADR-200, ADR-400]
---

# ADR-300: Canvas model: collapse/expand containment morph, snap-to-physics layout, and design language

## Context

hfsgraph is a canvas tool. The graph it draws is a containment tree (ADR-101) that must
stay legible at scale (~6,000 directories on a real target). It must also *feel* like a
deliberate spatial workspace (qpwgraph/ComfyUI lineage), not a generic node soup, and it
must read well on a Linux desktop alongside themed applications. This ADR fixes the visual
model: how containment is depicted and morphed, how layout avoids hairballs, and the
design language for the canvas itself.

## Decision

**Containment renders two ways; collapse/expand morphs between them.** This is the core
visual idea ("nested treemap meets force-directed graph"):

- **Expanded `[-]`** — children are separate nodes joined to the parent by containment
  edges (force-directed look).
- **Collapsed `[+]`** — the parent draws a perimeter that *swallows* its descendants,
  nesting them inside its own box; the edge becomes spatial enclosure (nested-treemap look).

Same relationship, two depictions, mixable per subtree. Consequently "force-directed tree"
and "nested treemap" are not separate modes but two ends of one interactive continuum.
Additional renderers (radial tidy tree, sunburst) remain available as alternate layouts.

**Two coordinate systems (ADR-101 reference).** *Layout position* (canvas x/y) is pure view
state, editable for every node including immutable ones, persisted in app state, never on
disk. *Structural position* (parent in the tree) changes only via a `move` op (ADR-200).
A canvas drag is therefore two gestures: **drag-to-arrange** (drop in empty space → layout
only) vs. **drag-to-rewire** (drop onto a node → emits a `move`, or bounces if illegal).

**Snap-to-physics layout (anti-hairball).** The force-directed layout settles then
*quantizes* — nodes snap to a clean arrangement (grid/ring/tier) rather than floating at
messy coordinates. Tree-aware forces exploit the single-parent invariant; layout is stable
and incremental (a pending move nudges, not re-throws, the board); collision/overlap
resolution and edge-bundling/orthogonal routing keep it followable; hand-placed nodes pin
and physics flows around them. Closer to constrained/quantized layout than naive d3-force.

**Design language.** A self-defined canvas aesthetic that conveys physical-space presence:

- **Canvas themes:** dark / light / twilight, selectable independent of node styling.
- **Background:** a rendered ground that gives spatial presence — grid lines, dots at
  intersections, or other tiled polygons — not a flat fill.
- **Nodes/edges float above the canvas** with drop shadows; robust, high-contrast object
  colors so groups/state read clearly against any background.
- **Not everything is a circle.** Directory nodes are rectangular containers showing a file
  listing; shape carries meaning (container vs. leaf, group, state).
- **Reuse native Qt/KF6 widgets** for non-canvas UI (lists, dialogs, toolbars) rather than
  reinventing them, and **minimize chrome/clutter** — the canvas is the interface.
- **Native theming with canvas override:** as a Qt6/KF6 app (ADR-400) the chrome inherits
  the KDE color scheme and widget style automatically; the canvas takes that palette as its
  default and layers its own themes (dark/light/twilight) on top.

## Consequences

### Positive

- One gesture (collapse/expand) unifies two visualization modes and manages scale —
  collapse dense subtrees into nested boxes, explode the area of interest.
- Separating layout from structure lets users tidy the picture (even of locked subtrees)
  without risk, and makes current-vs-proposed comparison stable.
- A deliberate design language with spatial-presence backgrounds and floating, high-contrast
  nodes makes a 6k-node graph feel navigable and intentional.

### Negative

- The collapse morph (edges ↔ nesting) plus snap-to-physics plus drop-shadow/float
  rendering is non-trivial custom drawing; more work than a stock node-editor widget.
- Custom canvas drawing sits on top of `QGraphicsView` (ADR-400); the morph, backgrounds,
  and drop-shadow/float styling are bespoke `QGraphicsItem` work, not stock widgets.
- Keeping 6k nodes smooth relies on `QGraphicsView`'s BSP indexing plus explicit
  level-of-detail (simplified item painting when zoomed out).

### Neutral

- Canvas theme, layout mode, collapse state, and node positions are view state persisted in
  the app store — a third persistence layer beside the sidecar state graph and the
  filesystem.
- Establishes that shape and color are semantic channels (container/leaf, group, immutable,
  volume, git boundary) shared across all layouts.

## Alternatives Considered

- **Use an existing node-editor widget** (litegraph, egui-snarl, QtNodes) — rejected for the
  core canvas: they are port/dataflow-oriented and fight the containment-nesting morph and
  the custom spatial aesthetic. Useful as references only.
- **Separate, switchable modes** for force-directed vs. treemap vs. tidy-tree instead of a
  single collapse continuum — rejected as the primary interaction: the morph is more
  powerful and intuitive; alternate layouts remain as secondary options.
- **Full native KDE/Qt widget theming** for the whole UI — deferred to ADR-400's stack
  decision; the canvas is custom-drawn regardless, so the gain would be confined to chrome.
- **Flat/minimal canvas (no background, no shadows)** — rejected: loses the sense of
  physical space that makes a large graph feel navigable.
