# Treemap interaction build — tracking (ADR-102 / 302 / 303, then 100 / 200)

Implementation of the decided design. The treemap viewer itself is **done** (ADR-301);
this file tracks the interaction/semantic layers on top. Slices 1–3 are buildable **now**
on the in-memory tree; 4–5 need new substrate. Mirrors the session task list (TaskList).

Design source of truth: `docs/architecture/` — ADR-101 (graph model), ADR-102 (groups),
ADR-200 (changeset engine), ADR-301 (treemap), ADR-302 (move staging), ADR-303 (frames).

## Slice 1 — Semantic groups (ADR-102)  [no new deps]  ✅ SHIPPED
- [x] Group data model + in-memory store (id, name, colour, kind=rule|manual, rule/members,
      exclusions, view-state: visible/highlight/dim/focus). — `core::Group` / `core::GroupStore`.
- [x] git-worktree **rule**: detect dirs containing `.git` (subdir or `.git` file); resolve
      members = anchor + all descendants − exclusions over the in-memory tree; re-resolve on
      rescan (idempotent, exclusions/colour/id preserved). — `core::resolveRuleGroups`.
- [x] Left **dock panel** of window-shade group cards (swatch, name, count, ▾ shade; per-group
      Show/Hi/Focus/Dim); depth-ramp legend at the bottom. — `ui::GroupPanel`.
- [x] Treemap **group overlay** in `TreemapItem` (highlight = tint/border in group colour; focus
      = dim non-members; dim = de-emphasise members; selection unchanged).
- [x] Wire panel ↔ scene ↔ overlay repaint (`GraphScene` owns the store, resolves on the scan
      root, `updateGroupOverlay()`; panel refresh on load + ramp change).
      *Debt: checkbox→overlay click path needs an interactive confirm (with pan/select/drill).*

## Slice 2 — Investigation frames (ADR-303)  [no new deps]  ✅ SHIPPED
- [x] `FrameItem`: draggable header (move/×) + interior treemap rooted at a subtree +
      ordered-dither drop shadow. Header chrome drawn device-space (screen-constant); dither
      tiled device-space (pixel-perfect, area scales with zoom). Shadow offset 18px.
- [x] Double-click opens a frame anchored to its origin square; diagonal **callout lines**
      (origin UR→frame UR, origin LL→frame LL; over base, under frame). Removed the
      double-click re-root and the Up/drill control (`drillInto/drillUp` gone). Guard: opens
      only a node strictly deeper than the treemap's own root (no self-stacking, no leaf).
- [x] Recursive frames (double-click inside a frame → child frame, shared scene) + z-order /
      click-to-raise (child-event filter) + close-cascade (closing a frame closes descendants).
      *Debt: drag / × / raise / recursion are interactive-confirm.*

## Slice 2.5 — Frames as the universal surface (ADR-304)  ✅ SHIPPED
- [x] **Resizable frames** — corner `ResizeGrip` (device-constant) → `TreemapItem::setSize`
      re-squarifies into larger bounds; constant-size labels elide less (magnifying lens).
- [x] **Per-level lens depth** — each lens scans its *own* subtree to `baseDepth + level`
      (capped `kMaxLensDepth=12`), independent of the base tree, owned by `FrameItem`
      (`unique_ptr<FsNode>`, explicit `~FrameItem` frees it — no shared-tree mutation, no leak).
- [x] **Cardinality 1** (default) — re-opening a node raises its existing frame
      (`setUniqueFrames` flag, match by path). Re-point updates callout origin + lineage.
- [x] **Zoom-from frustum callout** — convex hull of origin square ∪ frame, both rects
      subtracted (vertex occlusion), light device-tiled dither. Dynamic origin via
      `TreemapItem::cellRectForNode` (squarify replay, zoom-matched insets) → re-anchors on
      move/resize; scoped `refreshCalloutsFor` (affected chain only). Mode toolbar: On/Lines/Off.
- [x] **Crash fixes** — idempotent `closeFrame`; close on *release* not press (grabber-deletion
      race); `QPointer` deferred-close guard; `~FrameItem` deletes interior before owned tree.
- [x] **(task #19) Level 0 as a root frame** — base is now a level-0 `FrameItem` (title, resize
      grip, drop shadow, removable, no callout). `GraphScene::m_treemap`/`m_root`/`setRoot` gone;
      one `m_frames` vector holds bases + lenses, one render path top to bottom. Appearance
      changes (`setSizeMetric`/`setColorRamp`) now rebuild each interior in place
      (`FrameItem::rebuildInterior`) instead of destroying frames.
- [x] **(task #19) Multiple base surfaces** — `GraphScene::addBase/removeBase/clearBases`; each
      base frame owns its scanned tree (`unique_ptr`, RAII); `resolveRuleGroups` takes a *vector*
      of roots and resolves all bases in one pass, so adding/removing one never drops another's
      groups (verified: 3 groups across 2 bases, none dropped). `MainWindow` holds no tree state;
      depth change re-scans every base.
- [x] **(task #19) "Add base folder"** terminology; removable bases (× on the frame *and* in the
      dock); a **Bases** list at the top of the left dock, refreshed via `GraphScene::
      surfacesChanged`. *Interactive-confirm debt: base/lens drag-resize-close-raise + dock remove
      + multi-base lens open need a real-session click-through.*

## Slice 3 — Move staging (ADR-302)  [no new deps; Commit stubbed]
- [x] Move-op + **ledger** (ordered list) + **projection** = base tree with ops[0..k] replayed.
      `core::MoveOp`/`core::Ledger` (append-on-drop, undo/redo pops/pushes the tail, `setStep`
      scrub, no mid-list reorder) + `core::projectForest` (deep-copy replay over the *immutable*
      scanned forest, identity-keyed so ops survive later moves; skips unresolved/root/cycle/
      collision — safe replay floor; cross-base moves supported, ADR-304). `GraphScene` owns the
      ledger + projection and re-points each base frame's render root via `FrameItem::
      setRenderRoot` (`sourceRoot()` keeps the immutable identity for group resolution). Unit
      test `tests/move_test.cpp` (ctest) covers ledger + replay; verified end-to-end headlessly
      (a staged move re-squarifies the projection). *No gesture yet (#10); group overlay vs a
      non-empty projection awaits durable ids (#14).*
- [x] Drag gesture (#10): press a movable base-surface square → past a 6px threshold a scene-Z
      `MoveDragOverlay` draws `✕————▶` (legal) / `✕————✕` (illegal) with the target cell lit
      green/red; legality at drop via the new pure `core::checkMove` (SameNode/SourceIsRoot/Cycle/
      Collision — also the safe-replay floor `projectForest` now shares); release appends a
      `MoveOp` and re-squarifies the projection. The commit *defers* re-projection
      (`QTimer::singleShot`) because `setRenderRoot` deletes the interior treemap mid-release
      (grabber-deletion race). Drag identity is stable across earlier moves via a new
      `FsNode::identity` (pinned in the projection deep-copy; `keyFor` prefers it — the ADR-100
      seam), so re-moving an already-moved node resolves. Unit-tested (`checkMove` + chained-move
      identity); all three states verified headlessly. *Cross-volume legality not modelled (no
      volume id until ADR-100); lens→ drops are #13. Interactive mouse-path is confirm-debt.*
- [x] Bottom **queue dock** (#11): `ui::QueuePanel` in a bottom `QDockWidget`. A list with a
      `◆ Base` row + one row per staged op (`source → dest`); clicking a row **scrubs** the
      projection to that step (`GraphScene::scrubTo`), ops past the step shown italic/dimmed.
      **Undo/Redo/Clear** drive `GraphScene::undoMove/redoMove/clearMoves` (enable-states from
      the ledger); **Commit…** is a stub message (ADR-200 engine pending). New
      `GraphScene::ledgerChanged()` signal refreshes the panel (also on `surfacesChanged`).
      Verified headlessly (staged plan, scrub to an intermediate step, dimmed pending row).
      *Deferred: per-op confirmation "training wheels" (a modal mid-gesture needs care — would
      ride the endMoveDrag deferral); interactive mouse-path is confirm-debt.*
- [x] **Diff overlay** (#12): each square the active plan relocated gets an amber crosshatch
      + bold border + a step-number badge (matching the queue dock row). `TreemapItem` builds
      an identity→step map from `ledger().active()` each paint and `drawDiffMark`s matching
      cells on top (incl. over a moved dir's children); it tracks scrub/undo/redo since those
      re-project + repaint. Amber sits outside the depth ramps and group hues so it never reads
      as either. Verified headlessly (full plan badges 1+2; scrub to step 1 marks only op 1).
- [x] **Cross-frame** arrows + hit-testing (#13): the drag gesture now arms inside *any*
      surface (base or lens, not just bases) and the drop target is found on the **topmost**
      surface under the cursor (`surfaceCellAt` dropped its base-only filter; z-tiebreak picks
      the lens over the base where they overlap). The top-Z `MoveDragOverlay` already spanned
      the scene, so the ✕———▶ arrow stretches between frames. Lens node keys resolve against
      the base their subtree belongs to, so a lens drop stages correctly — the base + queue +
      diff overlay reflect it. *Known: a lens shows a static deep scan and does not itself
      re-flow after a staged move (the base is the authoritative staging surface); live lens
      projection would be a later enhancement.* Verified headlessly (base→lens legal arrow +
      commit; occluded-target correctly resolves to the topmost lens cell).

## UI/UX polish — interactive iteration (ADR-301/304)  [ongoing]
Ad-hoc rendering/control work from live use, on top of the slices above. All merged
unless noted; each had a code-reviewer pass.
- [x] **Groups panel → table** with multi-row select + bulk bar (Hi/No Hi/Show/Hide/Clear,
      All/None/Invert). Scales past the window-shade cards when many groups resolve.
- [x] **Viewport-sized base frames** — a new base matches the window aspect so fit-to-view
      fills it edge to edge (no manual enlarging).
- [x] **Pixel-icon LOD rung** + **file-type colour dictionary** — sub-icon cells show one
      tiny type-coloured dot per file; `ui/filetypestyle.{h,cpp}` is the single source of
      truth (`fileTypeIcon` + `fileTypeColor`), shared by dots / icons / names.
- [x] **Larger bounded canvas** — `updateSceneBounds` grows with content (4000px floor) to
      an 80000px cap, so exploring several bases doesn't hit the edge.
- [x] **Unified glyph spacing** — one `GlyphGrid {size,gap}` + `fitGlyphs` packer for the
      icon and dot (and list) rungs.
- [x] **Split LOD controls** — independent **Reveal** (subdivision/nesting) and **Detail**
      (contents crossover) toolbar sliders; they no longer fight.
- [x] **Forced file mode** (toolbar **Files**: Auto / Dots / Icons / List) overriding the
      size-driven rung; helpers self-guard so a forced rung hides on too-small cells.
- [x] **Fit names** — grow the map so a *typical* dir name renders untruncated: median-cell
      area × p90 name length, metric-aware, clamped 12×, idempotent (fit×N ≡ fit×1).
- [x] **Multi-column List rung** (`ls -a`): icon + type-coloured name, column-major, wraps
      into as many columns as fit (replaced the wasteful single-column names).
- [x] **Details (`ls -l`) file mode** — one file per row with metadata (perms/size/mtime/
      name/symlink). Scanner extended: `FsNode::files` (QStringList) → a `core::FileEntry`
      {name,sizeBytes,mtime,perms,isSymlink,linkTarget} vector (QFileInfo+QDateTime in the
      scanner); rippled to `group.cpp` (keyForFile/isWorktreeAnchor use `.name`),
      `treemapitem` (`files[i].name`, `.empty()`), `move.cpp` (vector copy, unchanged).
      Render: a force-only `Details` rung (toolbar **Files: Details**) — monospace meta
      column (perms via QFileDevice bits, human size via QLocale::formattedDataSize, mtime)
      + type-coloured icon + name; self-guards on width like the other rungs (verified
      headlessly: src/ cell shows aligned `ls -l` rows incl. a symlink's leading `l`).
- [x] **Tech-debt:** extracted the pure `squarify()` algorithm into `ui/squarify.{h,cpp}`
      (`treemapitem.cpp` 631 → 566 lines). Now a free function `ui::squarify`, unit-tested
      in isolation (`tests/squarify_test.cpp` — degenerate inputs, area∝weight conservation,
      containment, single-weight fill; second ctest target). treemapitem.cpp is still over
      the 500 flag (drawCell/drawLeafContents are the remaining bulk, but they're genuine
      TreemapItem methods, not a clean seam).
- [x] **Threaded scan (responsiveness fix)** — `Scanner::scan` ran synchronously on the UI
      thread; a cold/large/FUSE tree froze the app (measured ~32s for `~` at depth 2, with a
      `kg-fuse` mount under it). `MainWindow::scanAsync` runs the walk on a worker thread
      (`QtConcurrent::run` + `QFutureWatcher`), shows a busy cursor + "Scanning…" status, and
      hands the owned `FsNode` tree back on the GUI thread (the tree has no Qt-GUI deps, so
      off-thread build is safe). add-base, the startup load, and depth-change re-scan all route
      through it; concurrent scans are counted for the indicator. *Cancel/progress is a fuller
      follow-up if a tree is so big even the worker takes too long.*
- [ ] **Interactive-confirm debt (this session):** every new control's mouse path — groups
      table row-select + bulk buttons, the two LOD sliders, the Files combo, Fit names, base
      drag/resize/close/remove, panning the grown canvas. A real-session click-through.

## Slice 4 — Durable identity + persistence (ADR-100 / 102)
- [x] **(#14) ADR-100 durable identity substrate** — `platform/identity.{h,cpp}` (the only
      xattr/stat seam, ADR-400): `readDurableId`/`stampDurableId`/`xattrSupported` over the
      `user.hfsgraph.id` xattr, `statFingerprint` (lstat → `core::Fingerprint {dev,ino,mtime,
      size}`), `newDurableId` (QUuid), and lazy `ensureDurableId(FsNode&)`. The **scanner reads**
      the durable id + fingerprint per node — strictly read-only (a 6,000-dir tree mutates
      nothing). `FsNode` gains `identity` (now the real UUID, empty until stamped), `fp`, and
      `originalPath` (the scanned location). `keyFor` resolves to the on-disk UUID when present;
      both keying seams the UUID disturbed are fixed: `diffStepFor` now tests `path !=
      originalPath` (independent of the id scheme) and `keyForFile` keys off `keyFor(dir)` (the
      tracked path/identity asymmetry — retired). Unit-tested (`tests/identity_test.cpp`: real
      xattr round-trip + rename-survival + fingerprint + keyFor, self-skips on a non-xattr fs;
      `move_test` locks the originalPath "did it move?" contract). *Deferred to the commit engine
      (#16): wiring `ensureDurableId` into a live action — stamping at drag-stage time would write
      to disk during a preview, breaking "nothing touches disk until commit"; the commit engine is
      the correct touch point and the API is ready for it. Orphan re-adoption (last-path +
      fingerprint) lands with persistence (#15).*
- [ ] JSON group store persistence (XDG data dir, keyed by workspace durable id, id-keyed
      members/exclusions); reconcile rule exclusions against a changed tree on rescan.

## Slice 5 — Real commit engine (ADR-200, eventually Rust ADR-401)
- [ ] Commit: re-verify the whole queue vs current disk (drift), btrfs snapshot, apply moves in
      order, fsync, rollback on failure. All-or-nothing. Replaces the Slice-3 stub.

## Docs / polish
- [ ] Refresh `CONCEPT.md` to the treemap model (still describes node-link).
- [ ] Consider SPDX/REUSE headers on source files (KDE convention).

When a slice lands, tick it here and in `.claude/TODO.md`; delete this file once all slices ship.
