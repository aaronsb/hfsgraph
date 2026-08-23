---
name: drive
description: Drive, screenshot, bench, and profile the hfsgraph Qt app programmatically through its --script driver — headless (offscreen) or on the real display. Use when asked to run the app, see what the canvas looks like, check a UI change visually, measure frame time, or profile paint. Not for unit tests (make test) or for editing the driver itself.
---

# Drive hfsgraph

`hfsgraph --script FILE` runs `ui::Driver` (`src/ui/driver.h` holds the full grammar):
a line-oriented script that sends real input events and grabs PNGs. Headless with
`QT_QPA_PLATFORM=offscreen`; on the real display by leaving it unset (a window flashes
on screen). `$VAR` expands from the environment and an unset variable fails the line.

## Build

`make build BUILD_DIR=build-dev` — the repo's `build/` may hold a cache from a
previous checkout path; a fresh `BUILD_DIR` avoids the CMake mismatch. Binary:
`build-dev/hfsgraph`. Scripts write PNGs relative to the cwd, so run from the build dir.

## See the app

```
cd build-dev
cat > /tmp/look.txt <<'EOF'
resize 1200 800
load $HFSGRAPH_FIXTURE 3
wait
shot look.png
wheel 4
settle
shot look_zoomed.png view      # `view` = canvas viewport only
quit
EOF
HFSGRAPH_FIXTURE=$PWD/../src QT_QPA_PLATFORM=offscreen ./hfsgraph --script /tmp/look.txt
```

Then Read the PNG. `zoom F X Y` anchors at a viewport point; `wheel N` is centre-anchored.
`click/dblclick/drag X Y` hit the viewport; `key Space` goes to the view; `set
reveal|detail|filemode|ramp|metric|callout V` drive the toolbar; `fitnames` is the
Fit-names action.

## Measure frame time

`bench [N]` repaints the viewport N times and prints ms/frame for the *current* view
state. Bench each state you care about (overview, zoomed, forced rung, after
`fitnames`, high `reveal`) — the cost differs by an order of magnitude between them.
Measure on the real display at a realistic window size (`resize 3800 2000` on the
7680×2160 monitor) as well as offscreen: offscreen skips the compositor hand-off and
HiDPI, and has under-reported a 2× gap.

Synthetic tree: `tests/driver/make_synth.py /tmp/synth --depth 5` (≈6.5k dirs).
Real tree: `HFSGRAPH_FIXTURE=$HOME/Projects` at depth 3.

## Profile

```
perf record -q -F 1500 -g -o /tmp/p.data ./hfsgraph --script bench.txt
perf report -i /tmp/p.data --comm hfsgraph --children --stdio -q -g none --percent-limit 3 \
  | grep -E 'hfsgraph +\['
```

`--children` with the `hfsgraph` DSO filter attributes time to `paint → drawCell →
drawLeafContents / drawBackground`; Qt's own symbols are stripped, so read the
inclusive numbers on our frames. `kernel.perf_event_paranoid=2` is enough for this.

## Assert in ctest

`tests/driver/smoke.txt` is the pattern: `check bases N`, `check nonblank PNG`,
`check differs A B`; any failing line exits 2. Add a script + `add_test` in
`CMakeLists.txt` with `ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HFSGRAPH_FIXTURE=..."`.
