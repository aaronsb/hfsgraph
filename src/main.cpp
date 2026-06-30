// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// hfsgraph — a canvas tool for re-wiring a directory hierarchy to match its
// semantic structure.
//
// A squarified treemap of the scanned tree (nesting = containment) on a QGraphicsView
// canvas, with semantic level-of-detail zoom and floating investigation frames, plus a
// propose → verify → commit workflow over `mv`. See CONCEPT.md and ADR-301/303/304/400.
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
                     KAboutLicense::GPL_V3);
    // GPL-3.0-or-later to match the SPDX file headers (KF6 has no GPL_V3Plus enum; the
    // version restriction is how "or later" is expressed).
    about.setLicense(KAboutLicense::GPL_V3, KAboutLicense::OrLaterVersions);
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
