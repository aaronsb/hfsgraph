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
//   wait                     block until no scan is pending (then settle a frame)
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
//   dblclick X Y [MODS]
//   drag X1 Y1 X2 Y2 [MODS]  press, move in steps, release (left button)
//   set reveal|detail F      the LOD sliders' factor
//   set filemode|ramp|metric|callout N   the toolbar combos, by index
//   set fast 0|1             hold the interaction LOD (no leaf contents) on or off
//   shot PATH [view]         save a PNG of the main window, or of the canvas viewport only
//   check bases N            fail unless the scene holds N base surfaces
//   check nonblank PATH      fail unless the PNG at PATH has more than one colour
//   check differs A B        fail if the two PNGs are pixel-identical
//   bench [N]                repaint the viewport N times (default 20), print ms/frame
//   echo TEXT                print to stdout
//   quit                     exit 0 (a failed command exits 2)
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace ui {

class CanvasView;
class GraphScene;
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
    int m_pc = 0;           // next line to run
    int m_settle = 0;       // remaining idle turns before the next command
    int m_sleepMs = 0;      // wall-clock delay before the next command (`sleep`)
    bool m_waiting = false; // parked on `wait` until scans are idle
};

} // namespace ui
