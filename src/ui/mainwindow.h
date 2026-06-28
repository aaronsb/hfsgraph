// Top-level window: toolbar ("Add base folder", scan depth, appearance) over the
// canvas. The canvas can hold several base surfaces (ADR-304); each base frame owns
// its own scanned tree, so MainWindow keeps no tree state — it orchestrates scans
// and hands ownership to the scene.
#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QSpinBox;

namespace ui {

class CanvasView;
class GraphScene;
class GroupPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Scan `path` to `depth` (read-only) and add it as a base surface.
    void load(const QString &path, int depth);

  private:
    void addBaseFolder();                              // dialog → add a base
    void addBaseAtPath(const QString &path, int depth); // scan + hand to the scene
    void rescanAllBases(int depth);                   // depth changed: re-scan every base
    void updateStatus();                              // path label + window title

    CanvasView *m_view;
    GraphScene *m_scene;
    GroupPanel *m_groupPanel;
    QLabel *m_pathLabel;
    QSpinBox *m_depthSpin;
    QString m_currentPath; // last folder added, for the file dialog's start dir
};

} // namespace ui
