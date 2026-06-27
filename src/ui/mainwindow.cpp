#include "mainwindow.h"

#include "canvasview.h"
#include "core/scanner.h"
#include "graphscene.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

namespace ui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_scene = new GraphScene(this);
    m_view = new CanvasView(this);
    m_view->setScene(m_scene);
    setCentralWidget(m_view);

    auto *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);

    QAction *openAct = toolbar->addAction(QStringLiteral("Open Folder…"));
    connect(openAct, &QAction::triggered, this, &MainWindow::openFolder);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral(" Depth: ")));
    m_depthSpin = new QSpinBox(this);
    m_depthSpin->setRange(1, 12);
    m_depthSpin->setValue(2);
    m_depthSpin->setToolTip(QStringLiteral("Directory depth to scan"));
    connect(m_depthSpin, &QSpinBox::valueChanged, this, [this](int d) {
        if (!m_currentPath.isEmpty())
            load(m_currentPath, d);
    });
    toolbar->addWidget(m_depthSpin);

    toolbar->addSeparator();

    QAction *physicsAct = toolbar->addAction(QStringLiteral("Physics"));
    physicsAct->setCheckable(true);
    physicsAct->setToolTip(QStringLiteral("Animate the force layout live"));
    connect(physicsAct, &QAction::toggled, this,
            [this](bool on) { m_scene->setPhysicsRunning(on); });

    QAction *expandAct = toolbar->addAction(QStringLiteral("Expand all"));
    connect(expandAct, &QAction::triggered, this, [this] { m_scene->setAllShaded(false); });
    QAction *shadeAct = toolbar->addAction(QStringLiteral("Shade all"));
    connect(shadeAct, &QAction::triggered, this, [this] { m_scene->setAllShaded(true); });

    QAction *iconsAct = toolbar->addAction(QStringLiteral("Icons"));
    connect(iconsAct, &QAction::triggered, this, [this] { m_scene->setAllViewMode(0); });
    QAction *listAct = toolbar->addAction(QStringLiteral("List"));
    connect(listAct, &QAction::triggered, this, [this] { m_scene->setAllViewMode(1); });

    QAction *fitAct = toolbar->addAction(QStringLiteral("Fit to count"));
    fitAct->setToolTip(QStringLiteral("Resize every node to its object count"));
    connect(fitAct, &QAction::triggered, this, [this] { m_scene->fitAllToContent(); });

    m_pathLabel = new QLabel(this);
    statusBar()->addWidget(m_pathLabel);

    setWindowTitle(QStringLiteral("hfsgraph"));
    resize(1200, 800);
}

void MainWindow::openFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a directory to graph"),
        m_currentPath.isEmpty() ? QDir::homePath() : m_currentPath);
    if (!dir.isEmpty())
        load(dir, m_depthSpin->value());
}

void MainWindow::load(const QString &path, int depth) {
    m_currentPath = path;
    if (m_depthSpin->value() != depth) {
        QSignalBlocker block(m_depthSpin);
        m_depthSpin->setValue(depth);
    }

    m_root = core::Scanner::scan(path, depth);
    if (!m_root) {
        m_pathLabel->setText(QStringLiteral("Cannot read: %1").arg(path));
        m_scene->setRoot(nullptr);
        return;
    }
    m_scene->setRoot(m_root.get());
    m_view->resetTransform();
    if (m_scene->itemsBoundingRect().isValid())
        m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    m_pathLabel->setText(QStringLiteral("%1   (depth %2)").arg(path).arg(depth));
    setWindowTitle(QStringLiteral("hfsgraph — %1").arg(path));
}

} // namespace ui
