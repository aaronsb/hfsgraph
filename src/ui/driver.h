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
// environment):
//   load PATH DEPTH          add PATH as a base, scanned to DEPTH
//   wait                     block until no scan is pending (then settle a frame)
//   settle [N]               spin N event-loop turns (default 3) — let paints land
//   resize W H               resize the main window
//   fit                      fit every surface in the view
//   zoom F [X Y]             scale the view by F, anchored at viewport (X,Y) or centre
//   wheel N [X Y]            N wheel notches (negative = out) at viewport (X,Y)
//   pan DX DY                scroll the viewport by device pixels
//   key NAME                 press+release a key (QKeySequence name: Space, Ctrl+A…)
//   keydown NAME / keyup NAME
//   click X Y [MODS]         left click at viewport (X,Y); MODS = ctrl|shift|alt, '+'-joined
//   dblclick X Y [MODS]
//   drag X1 Y1 X2 Y2 [MODS]  press, move in steps, release (left button)
//   set reveal|detail F      the LOD sliders' factor
//   set filemode|ramp|metric|callout N   the toolbar combos, by index
//   shot PATH                save a PNG of the main window
//   check nonblank PATH      fail unless the PNG at PATH has more than one colour
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

    QString expand(const QString &line) const; // $VAR from the environment

    MainWindow *m_window;
    CanvasView *m_view;
    GraphScene *m_scene;
    QStringList m_lines;
    int m_pc = 0;           // next line to run
    int m_settle = 0;       // remaining idle turns before the next command
    bool m_waiting = false; // parked on `wait` until scans are idle
};

} // namespace ui
