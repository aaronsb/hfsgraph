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

## Slice 3 — Move staging (ADR-302)  [no new deps; Commit stubbed]
- [ ] Move-op + **ledger** (ordered list) + **projection** = base tree with ops[0..k] replayed.
- [ ] Drag gesture: pick up square/group → overlay arrow `✕————▶` (legal) / `✕————✕` (illegal),
      target hitbox lit, legality at drop (cycle / collision / immutable / cross-volume); release
      appends an op and re-squarifies to the projection.
- [ ] Bottom **queue dock**: numbered ops + status; click row → preview that step (scrub);
      undo/redo (append + pop tail); **Commit** button (stub until the engine); per-op
      confirmation setting (training wheels, default-on first run).
- [ ] **Diff overlay**: crosshatch + step-number tag on squares touched by the plan.
- [ ] **Cross-frame** arrows + hit-testing on a top overlay layer (needs Slice 2 frames).

## Slice 4 — Durable identity + persistence (ADR-100 / 102)
- [ ] ADR-100 durable directory identity (UUID in xattr + inode fingerprint) in the scanner.
- [ ] JSON group store persistence (XDG data dir, keyed by workspace durable id, id-keyed
      members/exclusions); reconcile rule exclusions against a changed tree on rescan.

## Slice 5 — Real commit engine (ADR-200, eventually Rust ADR-401)
- [ ] Commit: re-verify the whole queue vs current disk (drift), btrfs snapshot, apply moves in
      order, fsync, rollback on failure. All-or-nothing. Replaces the Slice-3 stub.

## Docs / polish
- [ ] Refresh `CONCEPT.md` to the treemap model (still describes node-link).
- [ ] Consider SPDX/REUSE headers on source files (KDE convention).

When a slice lands, tick it here and in `.claude/TODO.md`; delete this file once all slices ship.
