#include "mainwindow.h"

#include "canvasview.h"
#include "core/fsnode.h"
#include "core/scanner.h"
#include "frameitem.h"
#include "graphscene.h"
#include "grouppanel.h"
#include "queuepanel.h"

#include <memory>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>

namespace ui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_scene = new GraphScene(this);
    m_view = new CanvasView(this);
    m_view->setScene(m_scene);
    setCentralWidget(m_view);

    auto *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);

    QAction *openAct = toolbar->addAction(QStringLiteral("Add base folder…"));
    openAct->setToolTip(QStringLiteral("Add a directory tree as a base surface (ADR-304)"));
    connect(openAct, &QAction::triggered, this, &MainWindow::addBaseFolder);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral(" Depth: ")));
    m_depthSpin = new QSpinBox(this);
    m_depthSpin->setRange(1, 12);
    m_depthSpin->setValue(2);
    m_depthSpin->setToolTip(QStringLiteral("Directory depth to scan (re-scans every base)"));
    connect(m_depthSpin, &QSpinBox::valueChanged, this,
            [this](int d) { rescanAllBases(d); });
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

    auto *calloutCombo = new QComboBox(this); // GraphScene callout mode order
    calloutCombo->addItems({QStringLiteral("Zoom link: On"), QStringLiteral("Zoom link: Lines"),
                            QStringLiteral("Zoom link: Off")});
    calloutCombo->setToolTip(QStringLiteral("How a lens is tied to its origin square: filled "
                                            "frustum, hairlines, or nothing"));
    connect(calloutCombo, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_scene->setCalloutMode(i); });
    toolbar->addWidget(calloutCombo);

    auto *fileCombo = new QComboBox(this); // TreemapItem::FileMode order
    fileCombo->addItems({QStringLiteral("Files: Auto"), QStringLiteral("Files: Dots"),
                         QStringLiteral("Files: Icons"), QStringLiteral("Files: List"),
                         QStringLiteral("Files: Details")});
    fileCombo->setToolTip(QStringLiteral("How files in a cell are drawn: Auto picks by size "
                                         "(names → icons → dots), or force one (a forced rung "
                                         "is hidden on cells too small to fit it)"));
    connect(fileCombo, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_scene->setFileMode(i); });
    toolbar->addWidget(fileCombo);

    // Two independent LOD sliders (ADR-301): Reveal = how deep nesting subdivides;
    // Detail = at what cell size contents switch pixel-dots → icons → name. Same
    // mapping (mid = factor 1.0; higher = smaller gates = appears sooner). They were
    // one knob, but it forced a trade — revealing deeper nesting also promoted small
    // squares from dots to icons, and vice versa.
    auto addLodSlider = [this, toolbar](const QString &label, const QString &tip, auto setter) {
        toolbar->addWidget(new QLabel(label));
        auto *s = new QSlider(Qt::Horizontal, this);
        s->setRange(0, 100);
        s->setValue(50); // mid → factor 1.0 (the baseline thresholds)
        s->setFixedWidth(110);
        s->setToolTip(tip);
        connect(s, &QSlider::valueChanged, this,
                [this, setter](int v) { (m_scene->*setter)(1.6 - 1.2 * (v / 100.0)); });
        toolbar->addWidget(s);
    };
    addLodSlider(QStringLiteral(" Reveal "),
                 QStringLiteral("How deep nesting subdivides on screen"),
                 &GraphScene::setReveal);
    addLodSlider(QStringLiteral(" Detail "),
                 QStringLiteral("Cell size at which contents switch dots → icons → name"),
                 &GraphScene::setDetail);

    QAction *fitNames = toolbar->addAction(QStringLiteral("Fit names"));
    fitNames->setToolTip(QStringLiteral("Grow the map so typical directory names fit "
                                        "untruncated (very long names still truncate); "
                                        "pan the larger canvas to explore"));
    connect(fitNames, &QAction::triggered, this, [this] { m_scene->fitNamesToTypical(); });

    // Left dock: semantic-group legend + controls (ADR-102).
    auto *groupDock = new QDockWidget(QStringLiteral("Groups"), this);
    groupDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_groupPanel = new GroupPanel(m_scene, groupDock);
    groupDock->setWidget(m_groupPanel);
    addDockWidget(Qt::LeftDockWidgetArea, groupDock);

    // Bottom dock: the staged move plan (ADR-302 #11) — list, scrub, undo/redo, commit.
    auto *queueDock = new QDockWidget(QStringLiteral("Move queue"), this);
    queueDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    m_queuePanel = new QueuePanel(m_scene, queueDock);
    queueDock->setWidget(m_queuePanel);
    addDockWidget(Qt::BottomDockWidgetArea, queueDock);

    m_pathLabel = new QLabel(this);
    statusBar()->addWidget(m_pathLabel);

    // Keep the status bar / title in step when a base is removed (dock × or frame ×).
    connect(m_scene, &GraphScene::surfacesChanged, this, &MainWindow::updateStatus);

    setWindowTitle(QStringLiteral("hfsgraph"));
    resize(1200, 800);
}

void MainWindow::addBaseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Add a base folder to the canvas"),
        m_currentPath.isEmpty() ? QDir::homePath() : m_currentPath);
    if (!dir.isEmpty())
        addBaseAtPath(dir, m_depthSpin->value());
}

void MainWindow::load(const QString &path, int depth) {
    if (m_depthSpin->value() != depth) {
        QSignalBlocker block(m_depthSpin); // setting the value here must not re-scan
        m_depthSpin->setValue(depth);
    }
    addBaseAtPath(path, depth);
}

void MainWindow::addBaseAtPath(const QString &path, int depth) {
    std::unique_ptr<core::FsNode> tree = core::Scanner::scan(path, depth);
    if (!tree) {
        m_pathLabel->setText(QStringLiteral("Cannot read: %1").arg(path));
        return;
    }
    m_currentPath = path;
    m_scene->setBaseDepth(depth); // lenses scan baseDepth + their level (ADR-304)
    m_scene->addBase(std::move(tree)); // the base frame takes ownership of the scan
    // The panel refreshes itself via GraphScene::surfacesChanged.
    m_view->resetTransform();
    if (m_scene->itemsBoundingRect().isValid())
        m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    updateStatus();
}

void MainWindow::rescanAllBases(int depth) {
    // Depth changed: re-scan every base at the new depth. Snapshot the current base
    // paths (the scene owns the trees), drop all surfaces, then re-add each freshly
    // scanned. Open lenses don't survive a depth change (they're rooted in the old
    // trees) — same as the pre-ADR-304 reload behaviour, now generalised to N bases.
    QStringList paths;
    for (FrameItem *b : m_scene->baseFrames())
        paths << b->node()->path;
    if (paths.isEmpty())
        return;
    m_scene->setBaseDepth(depth);
    m_scene->clearBases();
    for (const QString &p : paths) {
        std::unique_ptr<core::FsNode> tree = core::Scanner::scan(p, depth);
        if (tree)
            m_scene->addBase(std::move(tree));
    }
    m_view->resetTransform();
    if (m_scene->itemsBoundingRect().isValid())
        m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    updateStatus();
}

void MainWindow::updateStatus() {
    const std::vector<FrameItem *> bases = m_scene->baseFrames();
    const int n = static_cast<int>(bases.size());
    const int depth = m_depthSpin->value();
    if (n == 0) {
        m_pathLabel->clear();
        setWindowTitle(QStringLiteral("hfsgraph"));
    } else if (n == 1) {
        // The surviving base, not m_currentPath — which may name a base that was
        // just removed (status refreshes via GraphScene::surfacesChanged).
        const QString path = bases.front()->node() ? bases.front()->node()->path : QString();
        m_pathLabel->setText(QStringLiteral("%1   (depth %2)").arg(path).arg(depth));
        setWindowTitle(QStringLiteral("hfsgraph — %1").arg(path));
    } else {
        m_pathLabel->setText(QStringLiteral("%1 bases   (depth %2)").arg(n).arg(depth));
        setWindowTitle(QStringLiteral("hfsgraph — %1 bases").arg(n));
    }
}

} // namespace ui
