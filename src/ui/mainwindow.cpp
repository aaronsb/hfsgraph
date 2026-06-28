#include "mainwindow.h"

#include "canvasview.h"
#include "core/scanner.h"
#include "graphscene.h"
#include "grouppanel.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QSlider>
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

    auto *sizeCombo = new QComboBox(this); // TreemapItem::SizeMetric order
    sizeCombo->addItems({QStringLiteral("Size: Count"), QStringLiteral("Size: Bytes")});
    sizeCombo->setToolTip(QStringLiteral("What a square's area is proportional to"));
    connect(sizeCombo, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_scene->setSizeMetric(i); });
    toolbar->addWidget(sizeCombo);

    auto *colorCombo = new QComboBox(this); // TreemapItem::Ramp order
    colorCombo->addItems({QStringLiteral("Viridis"), QStringLiteral("Magma"),
                          QStringLiteral("Plasma"), QStringLiteral("Cividis"),
                          QStringLiteral("Turbo"), QStringLiteral("Spectrum")});
    colorCombo->setToolTip(QStringLiteral("Colour ramp (by nesting depth)"));
    connect(colorCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_scene->setColorRamp(i);
        m_groupPanel->refresh(); // keep the depth legend in sync with the ramp
    });
    toolbar->addWidget(colorCombo);

    toolbar->addWidget(new QLabel(QStringLiteral(" Detail ")));
    auto *lodSlider = new QSlider(Qt::Horizontal, this);
    lodSlider->setRange(0, 100);
    lodSlider->setValue(50); // mid → factor 1.0 (the baseline thresholds)
    lodSlider->setFixedWidth(120);
    lodSlider->setToolTip(QStringLiteral("View distance: higher reveals squares' contents from "
                                         "farther out (more detail at a given zoom)"));
    // Higher slider = farther view distance = smaller gates = factor < 1.
    connect(lodSlider, &QSlider::valueChanged, this,
            [this](int v) { m_scene->setLod(1.6 - 1.2 * (v / 100.0)); });
    toolbar->addWidget(lodSlider);

    // Left dock: semantic-group legend + controls (ADR-102).
    auto *groupDock = new QDockWidget(QStringLiteral("Groups"), this);
    groupDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_groupPanel = new GroupPanel(m_scene, groupDock);
    groupDock->setWidget(m_groupPanel);
    addDockWidget(Qt::LeftDockWidgetArea, groupDock);

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
        m_groupPanel->refresh(); // store was cleared — keep the panel in step
        return;
    }
    m_scene->setBaseDepth(depth); // lenses scan baseDepth + their level (ADR-304)
    m_scene->setRoot(m_root.get());
    m_groupPanel->refresh(); // rule groups were re-resolved against the new tree
    m_view->resetTransform();
    if (m_scene->itemsBoundingRect().isValid())
        m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    m_pathLabel->setText(QStringLiteral("%1   (depth %2)").arg(path).arg(depth));
    setWindowTitle(QStringLiteral("hfsgraph — %1").arg(path));
}

} // namespace ui
