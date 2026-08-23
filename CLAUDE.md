# hfsgraph

Qt6/KF6 C++20 treemap file manager. `CONCEPT.md` is the thesis; `docs/architecture/`
holds the ADRs (`make adr CMD="list"`); `.claude/ways/` holds project ways.

## Build and test

- `make build BUILD_DIR=build-dev` then `ctest --test-dir build-dev`. The checked-in
  `build/` may carry a CMake cache from an earlier checkout path; use a fresh dir.
- `make lint` (clang-format) currently fails on `main` in files nobody touched —
  format only the files you change until a repo-wide format commit lands.

## Seeing and measuring the app

The app is scriptable: `hfsgraph --script FILE` drives it through real input events,
grabs PNGs, reports ms/frame, and runs headless under `QT_QPA_PLATFORM=offscreen`.
Use the `drive` skill (`.claude/skills/drive/SKILL.md`) whenever a change needs to be
seen, timed, or profiled rather than reasoned about — it has the recipes. Grammar:
`src/ui/driver.h`. Screenshot assertions live in ctest (`tests/driver/smoke.txt`).

## Layout

- `src/core/` — pure model (scanner, groups, move ledger, commit). Qt Core only; tested.
- `src/platform/` — xattr identity, stat fingerprints.
- `src/ui/` — scene, frames, treemap renderer, panels, driver.
