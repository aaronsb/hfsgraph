// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Scripted UI driver: runs a line-oriented command script against the live window
// so the app can be exercised without a hand on the mouse — screenshot tests under
// ctest (QT_QPA_PLATFORM=offscreen), and an agent/CI driving the real display.
// Input goes through the same event paths a user's would (QTest mouse/key events on
// the viewport, QWheelEvent for zoom), so a script exercises the gesture code, not a
// parallel API. Commands run one per event-loop turn so each frame paints between
// them; `wait` parks until every in-flight scan has landed.
//
// Script grammar (one command per line; `#` comments; `$VAR` expands from the
// environment — an unset variable fails the line):
//   load PATH DEPTH          add PATH as a base, scanned to DEPTH
//   wait                     block until no scan or deepen is pending (then settle a frame)
//   settle [N]               spin N event-loop turns (default 3) — let paints land
//   sleep MS                 wall-clock pause with the event loop running (timers fire)
//   resize W H               resize the main window
//   fit                      fit every surface in the view
//   fitnames                 grow the bases so typical names fit (toolbar "Fit names")
//   zoom F [X Y]             scale the view by F, anchored at viewport (X,Y) or centre
//   wheel N                  N wheel notches (negative = out), anchored at the view centre
//   pan DX DY                scroll the viewport by device pixels
//   key NAME                 press+release a key (QKeySequence name: Space, Ctrl+A…)
//   keydown NAME / keyup NAME
//   click X Y [MODS]         left click at viewport (X,Y); MODS = ctrl|shift|alt, '+'-joined
//   click @NAME [MODS]       left click on the glyph of the file named NAME (see `stage`)
//   dblclick X Y [MODS]
//   drag X1 Y1 X2 Y2 [MODS]  press, move in steps, release (left button)
//   set reveal|detail F      the LOD sliders' factor
//   set filemode|ramp|metric|callout N   the toolbar combos, by index (filemode: the view
//                            style — 0 Icons, 1 List, 2 Details)
//   set deepen 0|1           lazy deepening of truncated cells on (default) or off
//   set fast 0|1             hold the gesture state (lazy weights frozen) on or off; a
//                            later wheel/drag re-arms the idle timer and releases it
//   set focus 0|1            layout focus (ADR-305) on or off (default), for comparison
//   shot PATH [view]         save a PNG of the main window, or of the canvas viewport only
//   check bases N            fail unless the scene holds N base surfaces
//   check nodes N            fail unless the bases hold at least N directories (prints the count)
//   mark                     remember the directory count; `check grew` fails unless it rose
//   probe [X Y]              print the deepest cell under a viewport point (path, files, children)
//                            and the file glyph there, if any, and that surface's layout
//                            focus; the point is remembered for the focus checks below
//   check selected N         fail unless exactly N files are selected (prints the count)
//   check focus PATH         fail unless the layout focus (ADR-305) of the surface under
//                            the last probe point (the viewport centre before any probe)
//                            is the directory whose path ends with PATH (a glob if PATH
//                            holds `*`), or `none` for no focus (prints it)
//   check focusdepth N       fail unless the focus is N directories below its base root
//                            (`N+` = at least N); `check focuspopped` fails unless it is
//                            shallower than at the last `mark`
//   check file NAME          fail unless a file with base name NAME is selected
//   check ops N              fail unless exactly N ops are staged in the ledger
//   stage rename X Y NEW     stage a rename of the file glyph at (X,Y) (bypasses the menu);
//                            prints staged/refused — assert with `check ops`
//   stage trash X Y          stage moving that file to the trash
//   stage move X Y DX DY     stage moving that file into the directory cell at (DX,DY)
//                            (X Y may be `@NAME`: the file by name in the first laid-out
//                            leaf cell holding it — survives a re-flow; `@DIR/NAME` pins
//                            the leaf whose path ends with DIR)
//   check nonblank PATH      fail unless the PNG at PATH has more than one colour
//   check differs A B        fail if the two PNGs are pixel-identical
//   check same A B           fail unless they are
//   bench [N]                repaint the viewport N times (default 20), print ms/frame
//                            (paint only — no compositor hand-off; one warm-up frame first)
//   echo TEXT                print to stdout
//   quit                     exit 0 (a failed command exits 2)
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>

namespace core {
struct FsNode;
}

namespace ui {

class CanvasView;
class GraphScene;
class TreemapItem;
class MainWindow;

class Driver : public QObject {
    Q_OBJECT
  public:
    Driver(MainWindow *window, QObject *parent = nullptr);

    // Load a script from a file ('-' = stdin). Returns false (with a message on
    // stderr) if it can't be read.
    bool loadScript(const QString &path);

    // Begin executing once the event loop runs. Exits the application at `quit`,
    // at end of script, or on the first failing command (exit code 2).
    void start();

  private:
    void step();                       // run the next command, then reschedule
    bool run(const QString &line);     // one command; false = failure
    void fail(const QString &message); // report + exit(2)
    void finish(int code);

    bool expand(const QString &line, QString &out) const; // $VAR; false if one is unset

    MainWindow *m_window;
    CanvasView *m_view;
    GraphScene *m_scene;
    QStringList m_lines;
    int m_pc = 0;               // next line to run
    int m_settle = 0;           // remaining idle turns before the next command
    int m_sleepMs = 0;          // wall-clock delay before the next command (`sleep`)
    bool m_waiting = false;     // parked on `wait` until scans are idle
    bool m_waitPainted = false; // `wait` forced its one repaint (deepen requests come from paint)
    int m_mark = -1;            // directory count at the last `mark`
    int m_markFocusDepth = -1;  // layout focus depth at the last `mark`
    QPoint m_probe{-1, -1};     // viewport point of the last `probe`; focus checks read
                                // the surface under it (the viewport centre before any)
    int countNodes() const;     // directories across every base's render tree
    // `@NAME` / `@DIR/NAME`: the file named NAME in the first laid-out leaf cell (any
    // surface) holding one, or in the leaf whose path ends with DIR. Null item when none.
    struct FileRef {
        TreemapItem *item = nullptr;
        const core::FsNode *dir = nullptr;
        int index = -1;
    };
    FileRef locateFile(const QString &ref) const;
    // The layout focus of the surface under the probe point (ADR-305: per surface).
    const core::FsNode *probedFocus() const;
    int focusDepth() const; // that focus's depth below its base root, -1 if none
};

} // namespace ui
