// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "driver.h"

#include "canvasview.h"
#include "graphscene.h"
#include "mainwindow.h"

#include <cstdio>

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

namespace ui {

namespace {

// "ctrl+shift" → modifier flags. Unknown tokens are ignored.
Qt::KeyboardModifiers parseMods(const QString &spec) {
    Qt::KeyboardModifiers m = Qt::NoModifier;
    for (const QString &tok : spec.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
        if (tok == QLatin1String("ctrl"))
            m |= Qt::ControlModifier;
        else if (tok == QLatin1String("shift"))
            m |= Qt::ShiftModifier;
        else if (tok == QLatin1String("alt"))
            m |= Qt::AltModifier;
    }
    return m;
}

// A key name as QKeySequence understands it ("Space", "Escape", "Ctrl+A", "F") →
// the key + its modifiers. Returns Qt::Key_unknown for an unparseable name.
std::pair<Qt::Key, Qt::KeyboardModifiers> parseKey(const QString &name) {
    const QKeySequence seq(name);
    if (seq.isEmpty())
        return {Qt::Key_unknown, Qt::NoModifier};
    const QKeyCombination combo = seq[0];
    return {combo.key(), combo.keyboardModifiers()};
}

} // namespace

Driver::Driver(MainWindow *window, QObject *parent)
    : QObject(parent), m_window(window), m_view(window->view()), m_scene(window->scene()) {}

bool Driver::loadScript(const QString &path) {
    QFile file;
    if (path == QLatin1String("-")) {
        if (!file.open(stdin, QIODevice::ReadOnly | QIODevice::Text)) {
            std::fprintf(stderr, "driver: cannot read stdin\n");
            return false;
        }
    } else {
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::fprintf(stderr, "driver: cannot open script %s\n", qPrintable(path));
            return false;
        }
    }
    QTextStream in(&file);
    while (!in.atEnd())
        m_lines << in.readLine();
    return true;
}

void Driver::start() {
    QTimer::singleShot(0, this, &Driver::step);
}

QString Driver::expand(const QString &line) const {
    static const QRegularExpression var(QStringLiteral("\\$([A-Za-z_][A-Za-z0-9_]*)"));
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString out = line;
    QRegularExpressionMatchIterator it = var.globalMatch(line);
    // Substitute from the end so earlier offsets stay valid.
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext())
        matches.prepend(it.next());
    for (const QRegularExpressionMatch &m : matches)
        out.replace(m.capturedStart(0), m.capturedLength(0), env.value(m.captured(1)));
    return out;
}

void Driver::step() {
    if (m_waiting) {
        if (m_window->pendingScans() > 0) {
            QTimer::singleShot(20, this, &Driver::step);
            return;
        }
        m_waiting = false;
        m_settle = 3; // let the post-scan fit + paint land before the next command
    }
    if (m_settle > 0) {
        --m_settle;
        QTimer::singleShot(0, this, &Driver::step);
        return;
    }
    while (m_pc < m_lines.size()) {
        const QString raw = m_lines.at(m_pc++);
        const QString line = expand(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (!run(line)) {
            fail(QStringLiteral("line %1: %2").arg(m_pc).arg(raw.trimmed()));
            return;
        }
        QTimer::singleShot(0, this, &Driver::step); // one command per loop turn
        return;
    }
    finish(0); // end of script
}

bool Driver::run(const QString &line) {
    const QStringList a =
        line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QString cmd = a.at(0).toLower();
    auto num = [&](int i, double fallback = 0.0) {
        return i < a.size() ? a.at(i).toDouble() : fallback;
    };
    auto has = [&](int n) { return a.size() > n; };
    QWidget *vp = m_view->viewport();
    auto viewPoint = [&](int xi, int yi) {
        return has(yi) ? QPoint(static_cast<int>(num(xi)), static_cast<int>(num(yi)))
                       : vp->rect().center();
    };

    if (cmd == QLatin1String("load")) {
        if (!has(1))
            return false;
        m_window->load(a.at(1), has(2) ? static_cast<int>(num(2)) : 2);
        return true;
    }
    if (cmd == QLatin1String("wait")) {
        m_waiting = true;
        return true;
    }
    if (cmd == QLatin1String("settle")) {
        m_settle = has(1) ? static_cast<int>(num(1)) : 3;
        return true;
    }
    if (cmd == QLatin1String("resize")) {
        if (!has(2))
            return false;
        m_window->resize(static_cast<int>(num(1)), static_cast<int>(num(2)));
        m_settle = 2;
        return true;
    }
    if (cmd == QLatin1String("fit")) {
        m_view->resetTransform();
        if (m_scene->itemsBoundingRect().isValid())
            m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        m_scene->refreshCallouts();
        return true;
    }
    if (cmd == QLatin1String("zoom")) {
        if (!has(1))
            return false;
        const double f = num(1);
        if (f <= 0.0)
            return false;
        // Anchor at the given viewport point the way AnchorUnderMouse would: keep the
        // scene point under it fixed across the scale.
        const QPoint p = viewPoint(2, 3);
        const QPointF before = m_view->mapToScene(p);
        m_view->scale(f, f);
        const QPointF after = m_view->mapToScene(p);
        const QPointF d = after - before;
        m_view->horizontalScrollBar()->setValue(
            m_view->horizontalScrollBar()->value() -
            static_cast<int>(d.x() * m_view->transform().m11()));
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->value() -
                                              static_cast<int>(d.y() * m_view->transform().m22()));
        m_scene->refreshCallouts();
        return true;
    }
    if (cmd == QLatin1String("wheel")) {
        if (!has(1))
            return false;
        const int n = static_cast<int>(num(1));
        const QPoint p = viewPoint(2, 3);
        const int notches = n < 0 ? -n : n;
        for (int i = 0; i < notches; ++i) {
            QWheelEvent ev(p, vp->mapToGlobal(p), QPoint(), QPoint(0, n < 0 ? -120 : 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(vp, &ev);
        }
        return true;
    }
    if (cmd == QLatin1String("pan")) {
        if (!has(2))
            return false;
        m_view->horizontalScrollBar()->setValue(m_view->horizontalScrollBar()->value() -
                                                static_cast<int>(num(1)));
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->value() -
                                              static_cast<int>(num(2)));
        m_scene->refreshCallouts();
        return true;
    }
    if (cmd == QLatin1String("key") || cmd == QLatin1String("keydown") ||
        cmd == QLatin1String("keyup")) {
        if (!has(1))
            return false;
        const auto [key, mods] = parseKey(a.at(1));
        if (key == Qt::Key_unknown)
            return false;
        m_view->setFocus();
        if (cmd == QLatin1String("key"))
            QTest::keyClick(vp, key, mods);
        else if (cmd == QLatin1String("keydown"))
            QTest::keyPress(vp, key, mods);
        else
            QTest::keyRelease(vp, key, mods);
        return true;
    }
    if (cmd == QLatin1String("click") || cmd == QLatin1String("dblclick")) {
        if (!has(2))
            return false;
        const QPoint p(static_cast<int>(num(1)), static_cast<int>(num(2)));
        const Qt::KeyboardModifiers mods = has(3) ? parseMods(a.at(3)) : Qt::NoModifier;
        QTest::mouseMove(vp, p);
        if (cmd == QLatin1String("click")) {
            QTest::mouseClick(vp, Qt::LeftButton, mods, p);
        } else {
            QTest::mouseClick(vp, Qt::LeftButton, mods, p);
            QTest::mouseDClick(vp, Qt::LeftButton, mods, p);
        }
        return true;
    }
    if (cmd == QLatin1String("drag")) {
        if (!has(4))
            return false;
        const QPoint from(static_cast<int>(num(1)), static_cast<int>(num(2)));
        const QPoint to(static_cast<int>(num(3)), static_cast<int>(num(4)));
        const Qt::KeyboardModifiers mods = has(5) ? parseMods(a.at(5)) : Qt::NoModifier;
        QTest::mouseMove(vp, from);
        QTest::mousePress(vp, Qt::LeftButton, mods, from);
        constexpr int steps = 8; // cross the drag threshold and give move handlers frames
        for (int i = 1; i <= steps; ++i) {
            const QPoint p = from + (to - from) * i / steps;
            QTest::mouseMove(vp, p);
            // QTest::mouseMove alone doesn't deliver a button-held move to the
            // scene; synthesise the move event with the button state set.
            QMouseEvent mv(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton,
                           mods);
            QCoreApplication::sendEvent(vp, &mv);
        }
        QTest::mouseRelease(vp, Qt::LeftButton, mods, to);
        return true;
    }
    if (cmd == QLatin1String("set")) {
        if (!has(2))
            return false;
        const QString what = a.at(1).toLower();
        const double v = num(2);
        const int i = static_cast<int>(v);
        if (what == QLatin1String("reveal"))
            m_scene->setReveal(v);
        else if (what == QLatin1String("detail"))
            m_scene->setDetail(v);
        else if (what == QLatin1String("filemode"))
            m_scene->setFileMode(i);
        else if (what == QLatin1String("ramp"))
            m_scene->setColorRamp(i);
        else if (what == QLatin1String("metric"))
            m_scene->setSizeMetric(i);
        else if (what == QLatin1String("callout"))
            m_scene->setCalloutMode(i);
        else
            return false;
        return true;
    }
    if (cmd == QLatin1String("shot")) {
        if (!has(1))
            return false;
        vp->repaint(); // a fresh frame before the grab, even mid-script
        const QPixmap pm = m_window->grab();
        if (!pm.save(a.at(1), "PNG")) {
            std::fprintf(stderr, "driver: cannot write %s\n", qPrintable(a.at(1)));
            return false;
        }
        std::printf("shot %s (%dx%d)\n", qPrintable(a.at(1)), pm.width(), pm.height());
        return true;
    }
    if (cmd == QLatin1String("check")) {
        if (!has(2) || a.at(1).toLower() != QLatin1String("nonblank"))
            return false;
        const QImage img(a.at(2));
        if (img.isNull())
            return false;
        QSet<QRgb> colours;
        for (int y = 0; y < img.height() && colours.size() < 2; y += 4)
            for (int x = 0; x < img.width() && colours.size() < 2; x += 4)
                colours.insert(img.pixel(x, y));
        if (colours.size() < 2) {
            std::fprintf(stderr, "driver: %s is blank\n", qPrintable(a.at(2)));
            return false;
        }
        return true;
    }
    if (cmd == QLatin1String("echo")) {
        std::printf("%s\n", qPrintable(line.mid(4).trimmed()));
        std::fflush(stdout);
        return true;
    }
    if (cmd == QLatin1String("quit")) {
        finish(0);
        return true;
    }
    std::fprintf(stderr, "driver: unknown command '%s'\n", qPrintable(cmd));
    return false;
}

void Driver::fail(const QString &message) {
    std::fprintf(stderr, "driver: failed at %s\n", qPrintable(message));
    finish(2);
}

void Driver::finish(int code) {
    m_pc = m_lines.size(); // no further steps, even if one is already queued
    std::fflush(stdout);
    QTimer::singleShot(0, qApp, [code] { QCoreApplication::exit(code); });
}

} // namespace ui
