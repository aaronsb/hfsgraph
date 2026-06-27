---
status: Draft
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-100, ADR-300]
---

# ADR-400: Technology stack: standalone Rust + egui, no embedded webview

## Context

hfsgraph is a standalone desktop tool that launches, does its work, and exits — not a
client/server app and **not** a file-manager plugin (Dolphin integration is explicitly out
of scope). Hard constraint from the outset: **no embedded web browser of any kind**
(rules out Electron, Tauri, Wails, and any CEF/webview approach). The app needs a custom
graph canvas (ADR-300), a safety-critical filesystem-mutation engine (ADR-200), and durable
xattr identity (ADR-100). Stated priorities: safe from dead-ends/unexpected behavior, no FFI
boundary to babysit, easy to build/iterate. A later, soft want surfaced: respect KDE visual
theming and use familiar widgets without clutter.

Candidates considered: (A) Qt6 + KDE Frameworks (C++), (B) Rust core + Qt UI via CXX-Qt,
(C) full Rust with egui.

## Decision

Build a **standalone Rust application using `egui`** (via `eframe`), single-language, no
embedded webview.

Rationale:

- **The Dolphin/KDE-integration reason for Qt is gone.** With plugin integration crossed
  off, the main argument that favored Qt/KF6 (native file-manager embedding) no longer
  applies. What remains are the stated priorities, which Rust single-language serves best.
- **Safety where it matters.** The dangerous part is the `mv`/commit engine (ADR-200);
  Rust's guarantees apply exactly there, and the FS ecosystem maps 1:1 onto our needs:
  `xattr`, `reflink-copy` (FICLONE), `rustix`/`statx`, `proc-mounts`, `uuid`,
  `libbtrfsutil` bindings.
- **No FFI dead-ends.** A single language avoids the CXX-Qt boundary the user wanted to
  avoid.
- **egui fits the canvas and the POC.** The canvas is custom-drawn regardless of toolkit
  (ADR-300's morph, grids/dots, floating drop-shadowed nodes); egui's immediate-mode painter
  is a strength for exactly this, and it gets a read-only viewer POC up fast.
- **The engine stays UI-agnostic** (ADR-200), so the UI is replaceable if this decision is
  ever revisited.

Build/run via a `make`-pattern Makefile wrapping `cargo` (build/test/lint/format) plus the
ADR/docs tooling — one discoverable control panel.

## Consequences

### Positive

- One language end to end; memory-safe engine; best-in-class Rust FS crates.
- Satisfies the no-webview constraint and the launch-do-exit model directly.
- Fast iteration for the POC; trivial `cargo`-based Makefile.

### Negative

- **Theming is the real cost.** egui does not inherit the KDE/Qt look-and-feel natively the
  way a Qt/KF6 app would. We honor the desktop by *reading* the system color scheme
  (e.g. `kdeglobals`) into the canvas palette (ADR-300) and shipping our own canvas themes —
  a best-effort bridge, not native theming. "Common Qt controls" become egui's own widgets.
  This is the **most revisitable** part of this decision.
- Immediate-mode rendering needs explicit culling/LOD to stay smooth at ~6k nodes.
- egui's stock widgets are less rich/polished than mature Qt widgets for non-canvas chrome.

### Neutral

- KDE/desktop integration is limited to launching and palette-reading; no plugin surface.
- Keeping the engine separable preserves a future option to reskin in Qt/KF6 without
  rewriting the core.

## Alternatives Considered

- **(A) Qt6 + KDE Frameworks (C++)** — strongest canvas at scale (QGraphicsView) and *native
  KDE theming + real Qt controls for free*, which directly serves the theming want. Rejected
  as primary because the Dolphin reason evaporated, it puts the dangerous engine in C++, and
  it conflicts with the single-language/no-FFI and fast-iteration priorities. **Retained as
  the explicit fallback if native KDE theming becomes a hard requirement** — the
  UI-agnostic engine makes that switch feasible.
- **(B) Rust core + Qt UI via CXX-Qt** — gets Rust safety *and* Qt theming, but reintroduces
  exactly the FFI boundary the user wanted to avoid; deferred unless theming forces Qt.
- **Any webview stack (Electron/Tauri/Wails)** — rejected outright by the no-embedded-browser
  constraint.
