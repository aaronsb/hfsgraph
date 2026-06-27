# hfsgraph — TODO (next session)

Picking-up notes. The POC is a working read-only graph viewer; everything below is
the next wave. See `CONCEPT.md` (roadmap + sharp edges) and `docs/architecture/` (ADRs).

## Known bugs / polish

- [ ] **Fit-to-count doesn't size the *detail/list* view correctly.** Icon mode fits
      fine; in list (detail) mode the node stays too narrow (columns clipped) and the
      height is off, so a scrollbar lingers. Manually widening a list node renders nicely —
      so `NodeItem::fitToContent()`'s detail branch needs: width sized to the QTreeView's
      column widths (name+size+type+date), and height = header row + rows·rowHeight + frame.
      (`src/ui/nodeitem.cpp`, `fitToContent()`.)
- [ ] Icon-grid fit still occasionally leaves a hair of vertical scroll on some counts —
      revisit the row/slack math now that the H-scrollbar is off.
- [ ] Consider SPDX/REUSE headers on source files (KDE convention) to match the GPL license.

## Next features (from CONCEPT roadmap)

- [ ] **Nested containment morph** — collapse `[+]` should *swallow* children into a nested
      perimeter (treemap-in-graph), not just hide them. ADR-300's signature visual.
- [ ] **Lazy expand** — scan/expand subtrees on demand so the full ~6k-node tree is
      navigable without loading it all up front.
- [ ] **Barnes-Hut** for the force sim — current charge pass is O(n²)/tick; quadtree
      approximation (θ≈0.9) for scale. Pairs with lazy expand.
- [ ] **Snap-to-physics** — settle → quantize to a clean grid/ring per ADR-300.

## Then: the actual point of the tool

- [ ] Start the **Rust core** behind the cxx boundary (ADR-401): scanner first, then the
      **ledger → verify → commit** mutation engine (ADR-200) with durable xattr identity
      (ADR-100), WAL + btrfs snapshot + idempotent replay.
- [ ] Semantic groups + associations overlay (ADR-101); `user.xdg.tags` interop, daemon-
      optional (ADR-100/400).
