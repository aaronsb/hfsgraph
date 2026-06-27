# hfsgraph

A canvas tool for **re-wiring a directory hierarchy to match its semantic structure**.

A hierarchical filesystem forces one rigid tree onto your files, but you think
semantically and multi-dimensionally. hfsgraph makes the semantic layer visible — groups,
colors, associations drawn on a node-graph canvas alongside the real directory tree — and
lets you re-align the physical structure to match, through a *propose → verify → commit*
workflow over `mv` (nothing touches disk until you Apply).

> **Status:** early. The concept and architecture are captured; the code is scaffold only.
> The first milestone is a **read-only graph viewer** (directories as nodes, containment as
> edges, with a collapse/expand "containment morph").

## Where things are

| What | Where |
|------|-------|
| Full concept / design | [`CONCEPT.md`](CONCEPT.md) |
| Architecture decisions | [`docs/architecture/`](docs/architecture/) — `make adr CMD="list --group"` |
| Tasks (build/lint/test) | `make help` |

## Stack

Standalone **Rust + egui** desktop app — no embedded webview, no client/server, launches
and exits. See [ADR-400](docs/architecture/platform/) for the rationale (and the theming
tradeoff vs. Qt/KDE).

## Quick start

```sh
make help     # list tasks
make build    # compile
make check    # lint + test + ADR lint
make adr CMD="list --group"
```
