// Top-level window: toolbar (open folder, scan depth) over the canvas.
#pragma once

#include "core/fsnode.h"

#include <QMainWindow>
#include <memory>

class QLabel;
class QSpinBox;

namespace ui {

class CanvasView;
class GraphScene;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Scan `path` to `depth` (read-only) and display it.
    void load(const QString &path, int depth);

  private:
    void openFolder();

    CanvasView *m_view;
    GraphScene *m_scene;
    QLabel *m_pathLabel;
    QSpinBox *m_depthSpin;
    QString m_currentPath;
    std::unique_ptr<core::FsNode> m_root;
};

} // namespace ui
