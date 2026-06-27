---
status: Accepted
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-100, ADR-300]
---

# ADR-400: Technology stack: standalone Qt6 + KDE Frameworks, no embedded webview

## Context

hfsgraph is a standalone desktop tool that launches, does its work, and exits — not a
client/server app and **not** a file-manager plugin (Dolphin integration is explicitly out
of scope). Hard constraint: **no embedded web browser of any kind** (rules out Electron,
Tauri, Wails, any CEF/webview). The app needs a custom graph canvas (ADR-300), a
safety-critical filesystem-mutation engine (ADR-200), and durable xattr identity (ADR-100).

Two priorities were weighed and they pulled in different directions:

- *Engine safety / single-language / fast iteration* favored Rust + egui.
- *Native KDE visual theming and familiar (Qt) controls* — explicitly requested — are
  effectively free in Qt/KDE Frameworks and not native in egui (egui would require a
  best-effort palette bridge and its own non-native widgets).

The deciding factor was **native KDE look-and-feel**: hfsgraph should feel like a
first-class citizen of the desktop it ships on, and the target environment is KDE/Arch with
the full Qt6 + KF6 stack already installed.

## Decision

Build a **standalone Qt6 + KDE Frameworks 6 application in C++20**, single-language, no
embedded webview. Build with **CMake + extra-cmake-modules**.

- **Canvas:** `QGraphicsView` / `QGraphicsScene` — purpose-built for thousands of
  interactive 2D items with BSP indexing, level-of-detail, zoom/pan, and hit-testing; the
  right substrate for the ~6,000-node target and the custom design language (ADR-300).
- **OS facilities from KF6:** `KF6::Solid` for mount/device/removable detection feeding the
  volume descriptors (ADR-200); `KF6::Baloo` for tag interop (daemon-optional, on top of the
  standard `user.xdg.tags` xattr — ADR-100/ADR-200); `KAboutData`/KConfig for app plumbing.
- **Engine deps:** `libbtrfsutil` for subvolume snapshots and reflink; `<sys/xattr.h>` for
  the durable UUID identity; `statx(2)` and `/proc/self/mountinfo` for fingerprints and
  volume typing.
- **Native theming:** the app inherits the KDE color scheme and widget style automatically;
  the canvas design language (ADR-300) layers its own themes on top.
- **Engine stays UI-agnostic** (ADR-200) — a pure module behind the dry-run/commit gate, so
  it remains independently testable regardless of the UI toolkit.

Build/run via a `make`-pattern Makefile wrapping CMake/CTest plus the ADR/docs tooling.

## Consequences

### Positive

- Native KDE theming and real Qt controls for free — satisfies the look-and-feel
  requirement directly, no palette-bridge hacks.
- `QGraphicsView` is the most proven canvas for this node count and interaction model.
- KF6 hands us the exact OS plumbing the design needs (Solid → volumes, Baloo → tags),
  rather than re-implementing mount/tag detection.
- Whole stack confirmed present on the target (Qt 6.11, KF6 6.27, ECM, libbtrfsutil).

### Negative

- The safety-critical `mv`/commit engine (ADR-200) is in C++, not a memory-safe language —
  the original safety motivation for Rust is given up. Mitigation: isolate the engine as a
  pure, exhaustively-tested module gated by the dry-run, with the btrfs snapshot as the
  catastrophic-undo net (the real safety mechanism, language-independent).
- C++ build/iteration is heavier than `cargo`; more CMake/toolchain surface to maintain.
- Ties the app to the KDE/Qt ecosystem (acceptable: it is the target environment).

### Neutral

- Single-language C++ avoids the CXX-Qt FFI boundary that a Rust-core + Qt-UI hybrid would
  introduce.
- The UI-agnostic engine preserves a theoretical future option to bind it elsewhere.

## Alternatives Considered

- **Standalone Rust + egui** — single-language, memory-safe engine, fast POC, excellent FS
  crates, satisfies no-webview. Rejected because egui does not provide native KDE theming or
  real Qt controls (only a best-effort `kdeglobals` palette bridge + non-native widgets), and
  native look-and-feel was the deciding requirement. Remains the fallback if the C++ engine
  proves a liability and native theming is later deprioritized.
- **Rust core + Qt UI via CXX-Qt** — would get Rust safety *and* Qt theming, but reintroduces
  the FFI boundary the project wanted to avoid; reconsider only if the engine's correctness
  in C++ becomes a real problem.
- **Any webview stack (Electron/Tauri/Wails)** — rejected outright by the no-embedded-browser
  constraint.
