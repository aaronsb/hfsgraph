// hfsgraph — a canvas tool for re-wiring a directory hierarchy to match its
// semantic structure.
//
// POC milestone (read-only viewer): directories as nodes, containment as edges,
// rendered on a QGraphicsView canvas with a collapse/expand containment toggle.
// See CONCEPT.md "First pass (MVP)" and ADR-300/ADR-400.
//
// Usage: hfsgraph [PATH] [DEPTH]
//   PATH   directory to graph (default: ~/Projects if present, else $HOME)
//   DEPTH  scan depth (default: 2)

#include "ui/mainwindow.h"

#include <QApplication>
#include <QDir>

#include <KAboutData>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    KAboutData about(QStringLiteral("hfsgraph"), QStringLiteral("hfsgraph"),
                     QStringLiteral("0.0.0"),
                     QStringLiteral("Re-wire a directory hierarchy to match its "
                                    "semantic structure"),
                     KAboutLicense::MIT);
    KAboutData::setApplicationData(about);

    const QStringList args = app.arguments();
    QString path = args.size() > 1 ? args.at(1) : QString();
    if (path.isEmpty()) {
        const QString projects = QDir::homePath() + QStringLiteral("/Projects");
        path = QDir(projects).exists() ? projects : QDir::homePath();
    }
    int depth = 2;
    if (args.size() > 2) {
        bool ok = false;
        const int d = args.at(2).toInt(&ok);
        if (ok && d > 0)
            depth = d;
    }

    ui::MainWindow window;
    window.show();
    window.load(path, depth);
    return app.exec();
}
