# Treemap interaction build — tracking (ADR-102 / 302 / 303, then 100 / 200)

Implementation of the decided design. The treemap viewer itself is **done** (ADR-301);
this file tracks the interaction/semantic layers on top. Slices 1–3 are buildable **now**
on the in-memory tree; 4–5 need new substrate. Mirrors the session task list (TaskList).

Design source of truth: `docs/architecture/` — ADR-101 (graph model), ADR-102 (groups),
ADR-200 (changeset engine), ADR-301 (treemap), ADR-302 (move staging), ADR-303 (frames).

## Slice 1 — Semantic groups (ADR-102)  [no new deps]
- [ ] Group data model + in-memory store (id, name, colour, kind=rule|manual, rule/members,
      exclusions, view-state: visible/highlight/dim/focus).
- [ ] git-worktree **rule**: detect dirs containing `.git`; resolve members = anchor + all
      descendants − exclusions over the in-memory tree; re-resolve on rescan.
- [ ] Left **dock panel** of window-shade group cards (swatch, name, count, expand; per-group
      visible/highlight/dim/focus/select; add/remove); depth-ramp legend at the bottom.
- [ ] Treemap **group overlay** in `TreemapItem` (highlight = tint/border in group colour; focus
      = dim non-members; selection).
- [ ] Wire panel ↔ scene ↔ overlay repaint.

## Slice 2 — Investigation frames (ADR-303)  [no new deps]
- [ ] `FrameItem`: draggable header (move/close) + interior treemap rooted at a subtree +
      ordered-dither drop shadow.
- [ ] Double-click opens a frame anchored near its origin square; diagonal **callout lines**
      (origin UR→frame UR, origin LL→frame LL; over parent, under child). Remove the
      double-click re-root and the Up/drill control (ADR-303 revises ADR-301).
- [ ] Recursive frames (double-click inside a frame → child frame) + z-order / click-to-raise.

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
