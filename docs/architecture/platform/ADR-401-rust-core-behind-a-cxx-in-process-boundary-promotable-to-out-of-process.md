---
status: Accepted
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-200, ADR-400]
---

# ADR-401: Rust core behind a cxx in-process boundary, promotable to out-of-process

## Context

The engine (scanner now; later the transactional `mv`/commit engine with dry-run legality,
xattr identity, btrfs snapshots) is the safety-critical part of hfsgraph. ADR-400 chose a
Qt6/KF6 C++ app for native theming, accepting C++ for the engine as a cost. We want to
reclaim memory safety for the engine by writing it in **Rust**, and to decide this *now* —
at iteration one, when the core only does read-only scanning — so the boundary pattern is
set while the payload is trivial, rather than retrofitted once dangerous ops are tangled
across it. The open question was whether a Rust core is sound here or a "death trap," and
if sound, whether the boundary should be **in-process** (linked) or **out-of-process** (a
separate service). Two independent research passes (in-process FFI; out-of-process IPC)
were run.

Key findings, converging:

- **In-process is sane *only* with plain `cxx`** (dtolnay) — 1.0, Chromium-proven, with a
  defined panic→C++-exception story. **cxx-qt** (KDAB, pre-1.0, churning build API) and
  hand-rolled `extern "C"` are the actual death traps; **UniFFI** has no C++ binding.
- **Out-of-process is sane *only* as stdio + newline-delimited JSON-RPC 2.0 + `QProcess`**
  (the LSP/rust-analyzer pattern). gRPC / Cap'n Proto / TCP are over-engineering for a
  single-user local tool moving <2 MB of tree data.
- **Fault isolation is *not* a valid reason to go out-of-process.** A process boundary
  gives no filesystem atomicity — the disk is shared mutable state either way. The
  transactional safety we need (write-ahead intent journal + snapshot-before-commit +
  idempotent replay) must be built into the engine *regardless of topology*.
- Out-of-process carries permanent extra cost (protocol surface, child lifecycle/supervision).
  In-process via `cxx` sets the same core/UI discipline at lower cost and is **promotable**
  to out-of-process later, because the `core/` API is the seam either way.

## Decision

Implement the engine as a **Rust core, linked in-process via `cxx`**, adopted at iteration
one in minimal form, with the boundary deliberately shaped to be **promotable** to an
out-of-process stdio/JSON-RPC service later.

- **Binding:** `cxx` only. Not cxx-qt (deep Qt-QObject integration; pre-1.0 churn), not raw
  `extern "C"`, not UniFFI.
- **Build:** **Corrosion** via `FetchContent`; the Rust crate is `crate-type = ["staticlib"]`,
  linked with `target_link_libraries`.
- **Boundary payload (message-shaped):** the scan result crosses as a flattened
  `rust::Vec<FlatNode>` of shared structs (parent index, `rust::String` name/path, file
  list) that the C++ side rebuilds into the `QGraphicsView` model. The contract is defined
  as plain serializable messages (request / result / streamed batch) — *not* shared
  pointers — so the identical contract can later run over a process boundary by swapping the
  transport. At ~6k nodes the copy is microseconds; zero-copy is unnecessary.
- **Threading:** scan runs off the Qt thread; results marshal back via a queued connection
  (`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`), keeping the UI responsive.
- **Error discipline:** every boundary function returns `Result<T, E>`; no panic escapes the
  boundary (cxx maps `Err`/panic to a C++ exception). Filesystem errors (EPERM/ENOSPC/EXDEV)
  are typed `Err` values, never aborts.
- **Transactional safety lives in the Rust core** (ADR-200): WAL/intent-journal +
  snapshot-before-commit + idempotent replay — independent of process topology.
- **Promotion is deferred,** triggered only by a concrete need (cross-process fault
  containment, killing a runaway scan without taking down the UI, or streaming at a scale
  that benefits). The migration is then "swap transport," not "redesign the API."
- **Hygiene:** run the FFI layer under ASan/UBSan in CI; pin crate versions.

## Consequences

### Positive

- Memory-safe engine for the dangerous future ops, set as the pattern at iteration one when
  the only payload is a read-only tree — the cheapest possible time to establish it.
- The message-shaped boundary keeps the out-of-process option open at near-zero extra cost.
- `cxx` + Corrosion is a mature, proven, low-UB path; avoids every pre-1.0/raw-C hazard.

### Negative

- Two-language codebase (Rust + C++/Qt) for a solo developer: every feature now needs both,
  and debugging straddles two toolchains with split backtraces.
- One-time, recurring-on-upgrade build tax (Corrosion + cxx codegen + Rust toolchain in CI).
- Partially revises ADR-400's "single-language C++" rationale: we re-introduce an FFI
  boundary, but a *thin, data-only* one (cxx), not the CXX-Qt hybrid ADR-400 rejected.

### Neutral

- The QGraphicsView model and all UI stay C++/Qt; only the engine is Rust.
- If the two-language cost proves not worth it, the `core/` seam allows reverting the engine
  to C++ without touching the UI.

## Alternatives Considered

- **Out-of-process Rust service (stdio + JSON-RPC + QProcess)** — sane, mirrors LSP, and is
  the eventual promotion target, but its fault-isolation selling point does not buy
  filesystem atomicity, and it adds permanent protocol + child-lifecycle cost not justified
  at iteration one. Deferred, not rejected — the chosen boundary is designed to become this.
- **cxx-qt (Rust QObjects)** — the Qt-native option, but pre-1.0 with a historically churning
  build API; adopting it now buys churn for no read-only-scan benefit. Reconsider only if the
  *model itself* later moves into Rust.
- **Raw `extern "C"` / cbindgen** — more footguns (manual lifetimes, panic→abort/UB) than
  `cxx`, which exists precisely to replace it.
- **UniFFI** — no C++ binding; not applicable.
- **Keep the engine in C++ (no Rust)** — simplest single-language path, but forgoes memory
  safety exactly where the app is most dangerous; the whole point of deciding now is to avoid
  a risky later migration.
