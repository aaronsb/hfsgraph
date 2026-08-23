---
status: Proposed
date: 2026-08-23
deciders:
  - aaronsb
  - claude
related: [ADR-301, ADR-303, ADR-304, ADR-302, ADR-200]
---

# ADR-305: Layout focus: viewport-anchored re-root of the layout with breadcrumb frames and hysteresis

## Context

ADR-301 fixed the canvas as one squarified treemap whose root rect is the surface, with
semantic LOD zoom revealing depth. That has a structural weakness the first real use
exposed: **the root rect never changes, so a deep cell is squarified inside whatever sliver
its ancestors left it.** Zooming into `a/b/c/d` does not give `d` a viewport-shaped rect; it
enlarges the same thin sliver until its children are cropped strips with no ancestry in
sight. ADR-303/304 answered "drill in" with a lens (a floating frame re-squarifying a subtree
into its own rect), but a lens is a deliberate, side-by-side tool — double-click, a second
surface, a callout — not what the wheel should do by itself. The project concept asks for the
opposite of that: a map you fall *into*, where every level fills the view as you arrive at it.

Issue #40 asked whether *continuous re-layout into the viewport* reads well. The spike
(PR #58; driver screenshots `driver_focus_{fit,in,deep,off,out}.png` from ctest
`driver_focus`) built it end to end, and its assessment plus the review on #58 taught us:

1. **Engage and descend read well.** At the second zoom step the focused directory fills the
   view under a breadcrumb (`synth_focus › d0_5 › d1_4`), its children squarish with full
   file lists; the canonical layout of the same moment (`driver_focus_off.png`) shows cropped
   slivers and no ancestry. A 220 ms morph of old→new cell rects makes engage feel like the
   cell expanding, not the map being replaced.
2. **Pop-to-parent as a floating overlay reads as an uninvited lens.** The spike kept the old
   focus's centre and area when popping, which placed the parent's overlay as a ~45 % mini-map
   in a corner *over a canonical map that is also the parent's layout* — two renderings of one
   directory at once (`driver_focus_out.png`). The operator judged this wrong from the
   screenshots, and the review found the cause structural, not a tuning problem: at the
   moment of release the parent's canonical cell already *is* the viewport's context, so a
   second squarification of it anywhere is a duplicate. The spike had a half-held position —
   a lens pinned in item space that also wants to behave like a re-root — and the pop is where
   the two halves collide.
3. **Leaves must never focus.** The first version pinned a 28-file leaf to the viewport;
   three more wheel notches put its file list off-screen and `driver_smoke` went blank. A
   leaf already crops its file grid to the viewport (ADR-301's rungs); pinning it only blows
   its body up.
4. **Engage/release must measure what is on screen.** The spike compared a cell's *size* to
   the viewport, so a focus panned off-screen never released (while still dimming the map), a
   cell larger than the viewport but mostly off-screen could engage, and once an overlay had
   zoomed past the viewport several children could each exceed 60 % of it — the descent then
   picked the first in weight order, not the one under the eye (review H2).
5. **Focus state on the interior item does not survive the ledger.** A staged op re-projects
   and recreates the `TreemapItem`, taking the focus, its rect and the morph maps with it; the
   next paint re-engages from scratch (review M1). Lazy deepening grafts in place and keeps
   the item, so the loss is specific to the ledger path. And with N surfaces each deciding a
   focus, one scene-wide pointer is overwritten by whichever painted last (M2).
6. **Focus changes what coordinates mean.** `driver_select` and `driver_zoom_inside` click at
   coordinates computed from the canonical layout and broke the moment focus moved things.

The hysteresis pair (60 % engage / 45 % release) never flapped in testing.

## Decision

**Layout focus exists and is on by default. It is a re-root of the *layout* — never of the
tree, the ledger, or the scene — and it belongs to the surface that decided it.**

- **Engage.** When one *subdivided* directory's cell's **visible intersection with the
  viewport** covers **60 %** of the viewport on both axes, it becomes the surface's layout
  focus: the canonical layout stops at that node (its cell becomes a flat *shadow*) and its
  subtree is re-squarified into a viewport-shaped rect, drawn over the canonical map. The
  decision runs at paint time from the current zoom and viewport, like every other LOD
  decision in ADR-301, but the reference rect is *passed in* by the surface rather than read
  off the paint widget, so a render-to-image path judges the same thing the screen does.
  Measuring the visible intersection (not the cell's size) means at most one child can
  qualify, so engage is a single descent to the deepest cell under the eye.
- **Release with hysteresis.** The focus releases when its visible intersection shrinks under
  **45 %** on either axis; the gap keeps a wheel burst from flapping at the threshold, and
  measuring visibility makes a pan away from the focus release it the same as a zoom out.
  Leaves are never a focus.
- **Focus state lives on the frame, keyed by identity.** The focus is a property of the
  `FrameItem` (the surface, ADR-304), not of the interior `TreemapItem`, and it is keyed by
  `MemberKey` — the same path identity the ledger and groups use (ADR-302) — not by node
  pointer. A re-projection recreates the interior and resolves the key against the new tree;
  the focus survives a drop, rename or scrub exactly as a lens survives them (ADR-303 "frames
  root on identity"). If the key no longer resolves (the focused directory was moved out of
  this surface's subtree) the focus clears. A lazy-deepening graft keeps the item and needs
  nothing. This also removes the dangling-pointer class the spike had (a scene-level raw
  `FsNode*` outliving a re-projection or a lens close).
- **One focus per surface.** Every surface (base or lens) decides its own focus against the
  viewport, independently, and owns it. There is no scene-wide focus; `GraphScene` asks the
  surface under a point when a caller (the driver's `probe`/`check focus`, a callout
  re-anchor) needs to know. A lens therefore focuses only once it is zoomed larger than the
  viewport; a base and a lens may each hold a focus at once.
- **The pop: the focus rect is the viewport — always (model b).** What happens when a focus
  releases is the point where the spike's model split. Two models were viable; (b) is chosen.

  **(a) Pop = clear to canonical, with the morph — not chosen.** Release returns the eye to the canonical
  map it had under it, animated; symmetric with engage (engage replaces canonical with the
  thing under the eye; release gives it back). The 60/45 gap already prevents flapping. It is
  the simplest model: it removes `focusRectAround` and the chained-pop cascade, and it keeps
  ADR-303's commitment that a focused view is a lens pinned in item space — the overlay's
  rect is captured at engage, panning shows canonical context around it, the scrim marks the
  lens. What (a) *keeps* is exactly that: one layout model (item-space rects, a surface that
  is its own root), with focus as a pinned overlay that comes and goes.

  **(b) The focus rect is the viewport — always — chosen.** The overlay is not pinned: it is
  re-squarified into the viewport's rect every time the focus changes, engage *and* release.
  Popping re-squarifies the *parent* into the viewport the same way engaging did; nothing is
  duplicated because there is only ever one map of any directory on screen, the one filling
  the view, and the canonical cells outside it are context rather than a second rendering.
  No scrim, no pinning, no `focusRectAround`. This is the "fall into the map" the concept
  asks for — each level fills the view on arrival and ascent mirrors descent. Its cost is
  real: a focused view is no longer in item space, which is in tension with ADR-303/304's
  item-space-pinned frames (callouts, lens anchoring and `rectFor` all assume a cell has one
  item-space rect), and the transition between a pinned lens and a viewport-anchored focus
  has to be designed rather than falling out of one mechanism. It also keeps a morph per
  pop, so a fast wheel-out still chains animations.

  **Chosen: (b).** It matches the product thesis — the treemap as a continuous zoomable map,
  not a map with lenses that appear by themselves — and it resolves Context §2 at the root
  rather than at the symptom. (a) stays recorded above as the fallback: if the item-space
  tension with ADR-303/304 proves unworkable in implementation, (a) can be adopted with the
  rest of this ADR unchanged, at the cost of zooming *out* of a deep directory going through
  slivers again. The visible-coverage rule above is the precondition for either.
- **Breadcrumb as real ancestor frame cells — live for every gesture.** Each ancestor of the
  focus is drawn as one thin frame cell around the overlay (a name strip plus a rim in the
  ancestor's depth colour), the innermost being the focus's parent. These are layout cells,
  not chrome, and this ADR owns what that means: a frame is **selectable** (left-click
  selects the ancestor), **droppable** (a drop onto a frame stages a move *into that
  ancestor*, ADR-302), **right-clickable** (the ancestor's directory menu), **draggable**
  (a press arms a move-drag of the ancestor; the base root remains guarded as today), and a
  callout origin. The rim is 6 px, so the most accidental place to release a drag near the
  focus edge is the one that moves a file two levels up; the drag-target highlight must
  therefore make the frame *visibly* the target (highlight the whole frame, not just the rim)
  before a drop can land on it. This is chosen over a single flattened bar (`a › b › c` in
  one strip, depth colours as chips) because the frame is honest about what it is — an
  ancestor *is* a cell that contains the focus — and it needs no second hit-test or drop
  path. The cost is compactness: at depth 6 the frames take ~100 px of top strips. Accepted
  for now and flagged as the first thing to revisit; collapsing outer frames into a bar past
  a depth is the escape hatch and does not change the decision.
- **Relation to lenses (ADR-303/304).** Focus is the *automatic, in-place, single-instance*
  lens: the same mechanism (re-squarify a subtree into a viewport-shaped rect) with no frame
  widget, no callout, and no second surface, driven by zoom instead of double-click. It
  replaces the lens for the "drill in and look" case; lenses remain the tool for side-by-side
  comparison, for keeping a subtree open while the view moves on, and for lens-depth scanning
  (ADR-304). The seam between them is `GraphScene::setLayoutFocus` (re-shaped to be
  per-surface): a future "pin focus" gesture takes the surface's current focus key and opens
  it as an ADR-303 lens, with no new mechanism. The pin is also the moment a
  viewport-anchored focus becomes an item-space-pinned frame, which is the clean boundary
  between the two commitments.
- **Lazy deepening (#36) and the interaction LOD.** No new mechanism. A truncated directory
  that becomes the focus gets viewport-shaped room at once, so its deepen request and the
  graft's re-layout arrive while the eye is on it; the overlay rebuilds through the same
  invalidation path and the focus survives because it is keyed on identity. Under the
  interaction freeze the focus subtree's weights are frozen like everywhere else; the focus
  shadow does not subdivide, so the canonical side never requests a deepen the overlay is
  already driving. The visible change is that the *first* deepen is now seen — the spike's
  evidence is that this is the moment focus earns its place.
- **Hit-testing and `rectFor`.** The overlay cells (frames, focus, its subtree) are appended
  *after* the canonical cells, so the existing pre-order "last match is deepest" rule makes
  `cellAt`/`fileAt` resolve to the overlay over what it covers. The cells stay the single
  truth for hit-testing, callouts, band/file gestures and the driver probe; nothing
  special-cases focus. `rectFor` is **defined** for canonical cells and for nodes in the
  focus subtree (culled descendants replay inside the overlay). For any other node under
  focus — in particular a sibling of an ancestor — it is **unspecified**: the spike's
  assessment reported it replaying inside a frame's inner; the review reads the code as
  returning the canonical rect (frames are not indexed), possibly hidden under the scrim.
  Either way callers must not anchor to it; a callout to such a node is allowed to be wrong
  until a caller needs it, at which point it becomes a decision rather than an accident.
- **One rendering of a marked node.** The focus node's shadow and overlay cells share a node;
  selection outline, diff hatch/badge and group tint paint once, on the overlay cell.
- **Control.** Focus is on by default so it is seen. The driver's `set focus 0|1` is the
  comparison switch; `check focus PATH|glob|none` and `probe` report the focus of the
  surface under the probe point. The driver's glob must stop at `/` (or a `check focusdepth
  N` added) so that the pop and the deeper step are actually asserted, not only engage and
  clear. Tests and benches that reason about canonical coordinates disable focus.

## Consequences

### Positive

- Zooming into a deep directory yields a viewport-shaped, squarish map of it with its
  ancestry visible, where ADR-301 alone yields slivers. This is the "drill in" the treemap
  promised and the wheel could not deliver.
- Visible-coverage thresholds make hysteresis a statement about what is on screen: pan and
  zoom release the same way, and engage descends to the cell under the eye.
- Focus keyed by identity on the frame survives the ledger the way lenses do, and removes a
  dangling-pointer class; per-surface focus means the driver and callouts ask a surface, not
  a last-writer-wins scene field.
- View-only by construction: the scanned tree and the ledger (ADR-302) are untouched; every
  gesture resolves against the same cell vector.
- Breadcrumb frames are cells, so select-ancestor and drop-onto-ancestor come free, and the
  ADR says so rather than leaving it as an accident of `cellAt`.
- Hysteresis held at 60/45 without flapping in the driver run; the morph cost is one lerp per
  visible cell per frame.

### Negative

- **Double layout work under focus.** Each rebuild computes the canonical layout to the focus
  shadow *and* the overlay; a focus change rebuilds both and snapshots the shown rects for
  the morph, and the decision runs on every paint including the ~13 morph frames. Paint-time
  layout was already cheap, but this is a second squarify per rebuild.
- **Tests that click canonical coordinates must disable focus** (`set focus 0`), and every
  future screenshot or coordinate assertion has to say which layout it asserts against.
- **Morph cost and chained morphs.** 220 ms of animation per change; a fast wheel burst
  chains several (child engages as the parent's overlay grows past the viewport) and is
  slightly jumpy. A fast wheel-out chains pops the same way.
- **Frames as live targets** make a 6 px rim the most consequential drop zone on screen; the
  highlight requirement mitigates but does not remove the risk of a two-levels-up move.
- A focused view is not in item space; ADR-303/304's pinned-frame
  assumptions (callout anchoring, `rectFor`) need a stated boundary, and the pin gesture is
  where that boundary is crossed.
- Leaves never focus, so a directory whose bulk is one huge leaf gets no help from focus.
- Depth-6+ breadcrumbs cost ~100 px of top strips.
- `rectFor` has a stated unspecified region under focus.

### Neutral

- Revises ADR-301's "the root rect is the surface": the *canonical* layout still is; the
  overlay is a second root over it — anchored to the viewport. Extends ADR-303/304: lenses are now the deliberate sibling of an
  automatic focus, focus state lives on `FrameItem` like every other surface property, and
  `setLayoutFocus` (per-surface) is where one becomes the other.
- Thresholds (60/45, both axes, visible intersection against the viewport rather than the
  surface) are the spike's values corrected per the review; a single-axis rule would engage
  more often on tall/wide directories and is not chosen.
- The focus shadow's draw rule (name only when titled, glyphs only when not) is an accident
  of the spike's condition and should be stated on purpose in the implementation.
- Sibling order on synthetic fixtures follows `readdir`, so focus assertions use a glob.

## Alternatives Considered

- **Pop by keeping the old focus's centre and area** (the spike's `focusRectAround`).
  Rejected on the operator's reading of `driver_focus_out.png` and the review's structural
  argument: a floating mini-map of the parent over a canonical map of the same parent. Stable
  but two renderings of one thing.
- **Keep the floating pop but hide the canonical cells under the parent's subtree.** Rejected:
  hides the duplication without removing it.
- **Measure engage/release on cell size** (the spike). Rejected: lets an off-screen focus
  hold, and lets a cell mostly off-screen engage; visible intersection fixes all three review
  cases with one change.
- **Focus state on the interior `TreemapItem`, keyed by node pointer** (the spike). Rejected:
  lost on every staged op and dangles across re-projection and lens close; identity on the
  frame is what ADR-303 already does for lenses.
- **One scene-wide focus.** Rejected: N surfaces writing one field is last-painter-wins.
- **Flattened breadcrumb bar.** The compact choice; rejected for now because it needs its own
  hit-test and drop path and cannot be a callout origin. Remains the fallback for deep trees.
- **Breadcrumb frames as chrome only** (skip `focusFrame` cells in `cellAt`'s callers).
  Rejected: the flag exists to allow it, but it makes the breadcrumb a picture of an
  ancestor instead of the ancestor, and drop-into-ancestor is the gesture a breadcrumb is
  for.
- **Let leaves focus.** Rejected by evidence: pinning a leaf to the viewport blanks the view
  under further zoom, and leaves already crop their file grid.
- **Focus as a lens** (engage opens an ADR-303 frame automatically). Rejected: a frame widget
  appearing on every zoom is the "uninvited lens" problem by design; lenses stay deliberate,
  with "pin focus" as the bridge.
- **Default off.** Rejected: the behaviour needs to be seen to be judged; the driver flag
  covers comparison and tests.
