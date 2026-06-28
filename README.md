# hfsgraph

A canvas tool for **re-wiring a directory hierarchy to match its semantic structure**.

A hierarchical filesystem forces one rigid tree onto your files, but you think
semantically and multi-dimensionally. hfsgraph makes the semantic layer visible — groups,
colors, associations drawn over the real directory tree — and lets you re-align the
physical structure to match, through a *propose → verify → commit* workflow over `mv`
(nothing touches disk until you Apply).

The canvas is a **squarified treemap**: a directory is a rectangle subdivided among its
children, so nesting *is* containment — no edges to route. **Semantic level-of-detail zoom**
reveals deeper structure as you zoom in (no fixed render depth), and the contents of a
directory — its child directories and, at the leaf, its files as icons — populate as cells
grow large enough on screen.

![hfsgraph: a squarified treemap with a git-worktree group highlighted in red (via the left Groups dock), and a floating investigation frame magnifying that subtree — tied back to its origin square by a light dithered "zoom-from" frustum — revealing deeper directories the base view doesn't](screenshots/treemap-overview.png)

*The treemap with the semantic layer live: a git-worktree **group** highlighted red from the
left **Groups** dock (Show / Hi / Focus / Dim), and a floating **investigation frame** —
a non-destructive lens that magnifies a subtree and scans it deeper than the base — tied to
its origin square by a dithered "zoom-from" frustum. Area ∝ subtree file count, colour by
nesting depth, file icons in leaf directories. (Groups: [ADR-102](docs/architecture/core/);
treemap: [ADR-301](docs/architecture/ui/); frames & lenses: [ADR-303](docs/architecture/ui/),
[ADR-304](docs/architecture/ui/).)*

## Where things are

| What | Where |
|------|-------|
| Full concept / design | [`CONCEPT.md`](CONCEPT.md) |
| Architecture decisions | [`docs/architecture/`](docs/architecture/) — `make adr CMD="list --group"` |
| Tasks (build/lint/test) | `make help` |

## Stack

Standalone **Qt6 + KDE Frameworks 6** desktop app (C++) — no embedded webview, no
client/server, launches and exits. Native KDE theming and controls; a `QGraphicsView` canvas
with a custom treemap item that does level-of-detail rendering in `paint()`; `KF6::Solid` for
mount/device detection, `KF6::Baloo` + `user.xdg.tags` for tag interop, `libbtrfsutil` for
snapshots. See [ADR-400](docs/architecture/platform/) for the rationale.

## Quick start

Requires Qt6, KDE Frameworks 6, extra-cmake-modules, and a C++20 compiler.

```sh
make help     # list tasks
make build    # configure + compile (CMake)
make run      # launch
make check    # lint + test + ADR lint
make adr CMD="list --group"
```

## Status

Early. A working **read-only treemap viewer**: the directory tree as a squarified,
semantic-LOD-zoom treemap with file icons in leaf cells, a title-bar/contents colour split,
selectable area metric (file count / bytes on disk), data-viz colour ramps
(Viridis/Magma/Plasma/Cividis/Turbo/Spectrum), a Detail "view distance" slider, middle-drag
pan, and click-to-select / double-click-to-drill navigation. Semantic **groups/tagging** and
the *re-wiring* engine (propose → verify → commit over `mv`) are the next phases — see
`.claude/TODO.md`, `CONCEPT.md`, and [ADR-301](docs/architecture/ui/).

## License

GPL-3.0-or-later — see [`LICENSE`](LICENSE). Chosen to align with the KDE ecosystem this
app is built on (Qt6 / KDE Frameworks).
