# hfsgraph — TODO (next session)

Picking-up notes. The POC is a working read-only graph viewer; everything below is
the next wave. See `CONCEPT.md` (roadmap + sharp edges) and `docs/architecture/` (ADRs).

## Known bugs / polish

- [x] **Fit-to-count doesn't size the *detail/list* view correctly.** Fixed: the root
      cause was async — `fitAllToContent()` opened the viewer and fit in the same pass, so
      `QFileSystemModel` had no rows yet and the measurements fell back to header-only
      widths / an undersized row height, then the real data overflowed. Now `fitToContent()`
      defers the list sizing to a new `applyListFit()` (wired to `QFileSystemModel::
      directoryLoaded`, or run immediately if rows already present): width = the four
      content-fit columns + a scrollbar-width gutter, height = header + rows·(rowHeight+1).
      (`src/ui/nodeitem.cpp`.)
- [x] Icon-grid fit left a hair of vertical scroll on some counts. Fixed: `fitToContent()`'s
      icon branch derived rows from an *optimistic* column count (full width / cellW, ignoring
      the scrollbar), so the view actually packed one fewer column, needed an extra row, and
      overflowed the height → stray scrollbar → which stole the width that dropped the column
      (a feedback loop). Now `fitCols` is pessimistic (subtracts the real `PM_ScrollBarExtent`
      gutter + a comfort margin), so rows are never under-counted and the height always fits.
      Verified at 1:1 for counts 7/17/50 (clean) and 200 (height-clamped, vertical-only by
      design). (`src/ui/nodeitem.cpp`.)
- [ ] Consider SPDX/REUSE headers on source files (KDE convention) to match the GPL license.

## The pivot: treemap is the canvas (done this session)

The viewer was a node-link graph (cards + edges) with force / tidy-tree layouts. We proved
those out, then realized the data is a *strict containment tree* (ADR-101) where the wire and
the box are the same fact — so the squarified **treemap is the canvas**, full stop. The
node-link/force machinery is gone; the treemap is the only view.

- [x] **Treemap-only canvas** — squarified treemap (`treemapitem.{h,cpp}`); every dir is a
      rectangle subdivided among its children, area ∝ subtree file count, depth by colour,
      nesting *is* containment (no edges). `GraphScene` slimmed to "own root + drill".
- [x] **Removed** the force sim, the Reingold–Tilford node-link layout, Tree/Organic modes, and
      the `Physics`/`repel`/`attract`/mode-combo/Expand/Shade/Icons/List/Fit toolbar controls.
      Toolbar is now just **Open / Depth / Up**. (Node-link tidy-tree + edge-routing work this
      session was the bridge that got us here — gone but it earned its keep.)
- [x] **Semantic (LOD) zoom** — the recursion lives in `TreemapItem::paint()`, driven by the
      painter's zoom + exposed viewport: a cell subdivides into children only once it's big
      enough *on screen*, off-screen cells are culled, and labels/icons are drawn in device
      space so they stay a **constant screen size** (zoom reveals depth, doesn't enlarge pixels).
      No fixed render depth. Verified at fit and 5× (labels unchanged size, deeper cells appear).
- [x] **Icons in cells** — leaf dirs that are big enough draw their files as theme icons
      (`iconForName`, cached by suffix), the finest LOD rung. Unifies the old per-card icon grid
      into the map.
- [x] **Select + drill** — left-click selects a cell (highlight); double-click drills in; the
      **Up** button ascends (`drillInto`/`drillUp` re-root; parent pointers stop at the scan root).
      *(Verified rendering headlessly; the click/drill gestures need a real-session check.)*
- [x] **NodeItem parked** — `nodeitem.{h,cpp}` kept on disk but dropped from the build (commented
      in CMakeLists), to be revived as a click-to-inspect detail panel. *Will need adaptation —
      its `GraphScene` drag/`onNodeMoved` callbacks no longer exist.*

- [x] **Title bar / contents split + readable icons** — every cell now draws a title bar (the
      ramp identity colour) over a darker (dark mode) / lighter (light mode) contents area, so
      file icons sit on a low-key background instead of washing out against the fill.
- [x] **Size metric selector** — area ∝ subtree weight; toolbar "Size: Count | Bytes" picks the
      weight (`TreemapItem::SizeMetric`). Count emphasizes file-dense dirs; Bytes is the classic
      disk-usage map (large-file dirs dominate, file-count-heavy dirs shrink to slivers).
- [x] **Colour-ramp selector** — Viridis/Magma/Plasma/Cividis/Turbo (8-stop LUTs) + a categorical
      Spectrum (HSL hue cycle), mapped by nesting depth. Toolbar combo; default Viridis.
- [x] **Detail (view-distance) slider** — scales every LOD gate (subdivide, title, icons) live;
      higher = farther view distance (contents populate from smaller on-screen sizes). Paint-only
      (`TreemapItem::setLod`), so it updates on drag without a rebuild.

### Follow-ups — build order (slices 1–3 are buildable now on the in-memory tree)

> **1.** Semantic groups (ADR-102) → **2.** Investigation frames (ADR-303) → **3.** Move staging
> (ADR-302, Commit stubbed) → **4.** Durable identity + JSON persistence (ADR-100/102) → **5.**
> Real commit engine (ADR-200, eventually Rust ADR-401). Detailed task breakdown lives in the
> session task list (TaskList) and `.claude/todo-treemap-build.md`.

- [x] **Semantic groups (ADR-102)** — SHIPPED (Slice 1). `core::Group`/`GroupStore` model;
      `resolveRuleGroups` git-worktree rule (anchor = dir with `.git` subdir or file; members =
      anchor + all descendants − exclusions; idempotent re-resolve, exclusions/colour/id kept);
      `ui::GroupPanel` left dock of window-shade cards (swatch/name/count/▾ + Show/Hi/Focus/Dim)
      with a depth-ramp legend; `TreemapItem` overlay (highlight tint+border, focus dims
      non-members, dim de-emphasises members). `GraphScene` owns the store, resolves on the scan
      root (drill doesn't disturb membership), repaints via `updateGroupOverlay()`.
      *Still keyed by path, not ADR-100 durable id (Slice 4 / task #14); no JSON persistence yet
      (Slice 4 / task #15). Manual groups + add/remove UI deferred. Checkbox→overlay click path
      needs an interactive confirm.*
- [ ] **Move staging UX (ADR-302)** — drag a square or a *group*: overlay arrow (`✕————▶`
      legal / `✕————✕` illegal), no relayout during drag, target hitbox lit, release appends to a
      **queue**. Bottom dock lists numbered ops; the treemap shows the **projection** at the
      selected step (replay ops[0..k] = ADR-200 idempotent replay); scrub to animate the plan;
      touched squares get a crosshatch + step-number tag. **Commit** = verify whole queue vs
      disk, snapshot, apply atomically, rollback on failure (ADR-200). Queue editing = append +
      undo/redo + click-to-preview (no mid-list reorder). Per-op confirmation configurable,
      default-on first run. Groups follow for free (ADR-102 id-keying). The staging UX is
      buildable on the in-memory projection now; Commit stubs until the ADR-200 engine
      (eventually Rust, ADR-401) lands.
- [x] **Investigation frames (ADR-303)** — SHIPPED (Slice 2). Double-click opens a floating
      `FrameItem` (interior treemap of the subtree) *over* the canvas — no re-root; `drillInto/
      drillUp` and the Up control removed. Same scene, recursive (double-click inside a frame →
      child frame), draggable header, ordered-dither drop shadow (device-tiled, pixel-perfect,
      area scales; offset 18px), screen-constant header chrome, diagonal callout lines
      (origin UR→frame UR, origin LL→frame LL; over base, under frame). Click-to-raise; closing a
      frame cascade-closes its descendants; self-open guard (only nodes deeper than the root).
      *Frames not yet identity-pinned across moves/queue-scrub (needs Slice 3 ledger). Drag / × /
      raise / recursion are interactive-confirm.*
- [~] **Frames as the universal surface (ADR-304)** — PARTIAL (Slice 2.5). Shipped: resizable
      frames (corner grip → `TreemapItem::setSize`, magnifying lens); per-level lens depth (each
      lens scans its own subtree to `baseDepth+level`, capped 12, independent + `unique_ptr`-owned,
      no shared-tree mutation/leak); cardinality-1 (raise existing, `setUniqueFrames` flag);
      zoom-from frustum callout (convex-hull − rects, light dither, dynamic origin via
      `cellRectForNode` replay, scoped refresh, On/Lines/Off toolbar mode); and a run of crash
      fixes (idempotent `closeFrame`, close-on-release, `QPointer` deferred-close, `~FrameItem`
      deletes interior before owned tree). **Remaining (task #19):** level 0 becomes a root
      `FrameItem`; multiple base surfaces (multi-root group resolution; ledger spans them);
      "Add base folder" terminology + removable bases + bases list in the dock.
- [ ] **Lazy scan = truly unbounded depth** — semantic zoom can only reveal what's scanned; scan a
      subtree the first time you zoom into it, so the Depth control can go away entirely.
- [ ] **Treemap polish** — breadcrumb for the drill path; hover tooltip (full name + size);
      filenames under icons at the deepest LOD rung; maybe colour-by-value (not just depth).
- [x] **Docs drift** — README now describes the treemap (force-graph screenshot replaced); ADR-300
      superseded by ADR-301. *Still stale:* `CONCEPT.md` (roadmap + node-link prose).

## Then: the actual point of the tool

- [ ] Start the **Rust core** behind the cxx boundary (ADR-401): scanner first, then the
      **ledger → verify → commit** mutation engine (ADR-200) with durable xattr identity
      (ADR-100), WAL + btrfs snapshot + idempotent replay. This is the real Commit behind the
      ADR-302 staging UX.
- [ ] **Associations overlay** (ADR-101's node↔node half, with co-move policy) and `user.xdg.tags`
      / Baloo export bridge (ADR-100/400). (Groups — the other half — are slice 1 above.)
