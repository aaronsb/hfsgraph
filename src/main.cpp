// hfsgraph — a canvas tool for re-wiring a directory hierarchy to match its
// semantic structure.
//
// This is a placeholder entry point. The first real milestone is a read-only
// graph viewer (see CONCEPT.md "First pass (MVP)" and ADR-300): directories as
// nodes, containment as edges, with the collapse/expand containment morph,
// rendered on a QGraphicsView canvas.
//
// Architecture decisions live in docs/architecture/ (run `make adr CMD=list`).

#include <QApplication>
#include <QLabel>
#include <QMainWindow>

#include <KAboutData>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    KAboutData about(QStringLiteral("hfsgraph"), QStringLiteral("hfsgraph"),
                     QStringLiteral("0.0.0"),
                     QStringLiteral("Re-wire a directory hierarchy to match its "
                                    "semantic structure"),
                     KAboutLicense::MIT);
    KAboutData::setApplicationData(about);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("hfsgraph — scaffold"));
    window.setCentralWidget(
        new QLabel(QStringLiteral("hfsgraph scaffold. The read-only graph viewer POC is the next "
                                  "milestone\n(see CONCEPT.md and docs/architecture/).")));
    window.resize(640, 400);
    window.show();

    return app.exec();
}
