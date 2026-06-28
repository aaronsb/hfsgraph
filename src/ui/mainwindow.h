// Top-level window: toolbar ("Add base folder", scan depth, appearance) over the
// canvas. The canvas can hold several base surfaces (ADR-304); each base frame owns
// its own scanned tree, so MainWindow keeps no tree state — it orchestrates scans
// and hands ownership to the scene.
#pragma once

#include <QMainWindow>
#include <QString>

#include <functional>
#include <memory>

class QLabel;
class QSpinBox;

namespace core {
struct FsNode;
}

namespace ui {

class CanvasView;
class GraphScene;
class GroupPanel;
class QueuePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override; // restore any override cursor left by an in-flight scan

    // Scan `path` to `depth` (read-only) and add it as a base surface.
    void load(const QString &path, int depth);

  private:
    void addBaseFolder();                              // dialog → add a base
    void addBaseAtPath(const QString &path, int depth); // scan + hand to the scene
    void rescanAllBases(int depth);                   // depth changed: re-scan every base
    void updateStatus();                              // path label + window title

    // Scan `path` to `depth` on a worker thread (the walk can take tens of seconds on
    // a cold/large/FUSE tree and must not freeze the UI), then deliver the owned tree
    // to `onReady` back on the GUI thread. Tracks in-flight scans to drive the busy
    // indicator. The FsNode tree has no Qt-GUI deps, so building it off-thread is safe.
    void scanAsync(const QString &path, int depth,
                   std::function<void(std::unique_ptr<core::FsNode>)> onReady);
    void updateBusy();         // wait-cursor + status while any scan is in flight
    void fitToContentIfIdle(); // reset+fit the view, but only once the batch is done

    CanvasView *m_view;
    GraphScene *m_scene;
    GroupPanel *m_groupPanel;
    QueuePanel *m_queuePanel;
    QLabel *m_pathLabel;
    QSpinBox *m_depthSpin;
    QString m_currentPath; // last folder added, for the file dialog's start dir
    int m_pendingScans = 0; // in-flight async scans (drives the busy indicator)
    bool m_busyActive = false; // whether we've pushed an override cursor
    int m_queuedDepth = -1; // a depth change requested mid-scan; applied when idle (-1 = none)
};

} // namespace ui
