# hfsgraph

Qt6/KF6 C++20 treemap file manager. `CONCEPT.md` is the thesis; `docs/architecture/`
holds the ADRs (`make adr CMD="list"`); `.claude/ways/` holds project ways.

## Build and test

- `make build` then `make test` (or `BUILD_DIR=build-dev` for a side tree). `make configure`
  drops a `build/` cache that was made from another checkout path.
- `make lint` (clang-format 22) is green on `main`; run `clang-format -i` on the files
  you touch before committing so it stays that way.

## Seeing and measuring the app

The app is scriptable: `hfsgraph --script FILE` drives it through real input events,
grabs PNGs, reports ms/frame, and runs headless under `QT_QPA_PLATFORM=offscreen`.
Use the `drive` skill (`.claude/skills/drive/SKILL.md`) whenever a change needs to be
seen, timed, or profiled rather than reasoned about — it has the recipes. Grammar:
`src/ui/driver.h`. Screenshot and state assertions live in ctest (`tests/driver/*.txt`:
`check bases|nodes|grew|selected|file|ops|nonblank|differs|same`, `probe`, `stage`).

## Layout

- `src/core/` — pure model (scanner, groups, move ledger, commit). Qt Core only; tested.
- `src/platform/` — xattr identity, stat fingerprints.
- `src/ui/` — scene, frames, treemap renderer, panels, driver.
