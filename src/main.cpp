// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// hfsgraph — a canvas tool for re-wiring a directory hierarchy to match its
// semantic structure.
//
// A squarified treemap of the scanned tree (nesting = containment) on a QGraphicsView
// canvas, with semantic level-of-detail zoom and floating investigation frames, plus a
// propose → verify → commit workflow over `mv`. See CONCEPT.md and ADR-301/303/304/400.
//
// Usage: hfsgraph [--script FILE] [PATH] [DEPTH]
//   PATH      directory to graph (default: ~/Projects if present, else $HOME)
//   DEPTH     scan depth (default: 2)
//   --script  run a ui::Driver command script ('-' = stdin) instead of the default
//             load; pair with QT_QPA_PLATFORM=offscreen for headless screenshots.

#include "ui/driver.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>

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

    QCommandLineParser parser;
    parser.setApplicationDescription(about.shortDescription());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("Directory to graph"));
    parser.addPositionalArgument(QStringLiteral("depth"), QStringLiteral("Scan depth (default 2)"));
    const QCommandLineOption scriptOpt(QStringLiteral("script"),
                                       QStringLiteral("Run a driver command script (- = stdin)"),
                                       QStringLiteral("file"));
    parser.addOption(scriptOpt);
    parser.process(app);

    // Headless platforms (offscreen) carry no icon theme, so the file-icon rungs would
    // draw nothing; fall back to breeze when the platform didn't name one.
    if (QIcon::themeName().isEmpty())
        QIcon::setThemeName(QStringLiteral("breeze"));

    const QStringList args = parser.positionalArguments();
    QString path = args.size() > 0 ? args.at(0) : QString();
    if (path.isEmpty()) {
        const QString projects = QDir::homePath() + QStringLiteral("/Projects");
        path = QDir(projects).exists() ? projects : QDir::homePath();
    }
    int depth = 2;
    if (args.size() > 1) {
        bool ok = false;
        const int d = args.at(1).toInt(&ok);
        if (ok && d > 0)
            depth = d;
    }

    ui::MainWindow window;
    window.show();
    if (parser.isSet(scriptOpt)) {
        // Scripted: the script decides what to load and when to exit.
        auto *driver = new ui::Driver(&window, &window);
        if (!driver->loadScript(parser.value(scriptOpt)))
            return 2;
        driver->start();
        return app.exec();
    }
    window.load(path, depth);
    return app.exec();
}
