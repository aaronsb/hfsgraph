// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "driver.h"

#include "canvasview.h"
#include "core/fsnode.h"
#include "core/group.h"
#include "frameitem.h"
#include "treemaplayout.h"
#include "treemapitem.h"
#include "graphscene.h"
#include "selection.h"
#include "mainwindow.h"

#include <algorithm>
#include <cstdio>
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
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
#include <QUrl>
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

bool Driver::expand(const QString &line, QString &out) const {
    static const QRegularExpression var(QStringLiteral("\\$([A-Za-z_][A-Za-z0-9_]*)"));
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    out = line;
    QRegularExpressionMatchIterator it = var.globalMatch(line);
    // Substitute from the end so earlier offsets stay valid.
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext())
        matches.prepend(it.next());
    for (const QRegularExpressionMatch &m : matches) {
        const QString name = m.captured(1);
        if (!env.contains(name)) {
            // An unset variable silently expanding to "" turns `load $FIXTURE 3` into
            // `load 3` — a script that passes for the wrong reason. Fail instead.
            std::fprintf(stderr, "driver: $%s is not set\n", qPrintable(name));
            return false;
        }
        out.replace(m.capturedStart(0), m.capturedLength(0), env.value(name));
    }
    return true;
}

void Driver::step() {
    if (m_waiting) {
        if (!m_waitPainted) {
            // Deepen requests are issued by paint: force one so `wait` sees them rather
            // than returning before an offscreen repaint has landed.
            m_view->viewport()->repaint();
            m_waitPainted = true;
        }
        if (m_window->pendingScans() > 0 || m_scene->deepensInFlight() > 0) {
            QTimer::singleShot(20, this, &Driver::step);
            return;
        }
        m_waiting = false;
        m_waitPainted = false;
        m_settle = 3; // let the post-scan fit + paint land before the next command
    }
    if (m_settle > 0) {
        --m_settle;
        QTimer::singleShot(0, this, &Driver::step);
        return;
    }
    while (m_pc < m_lines.size()) {
        const QString raw = m_lines.at(m_pc++);
        if (raw.trimmed().isEmpty() || raw.trimmed().startsWith(QLatin1Char('#')))
            continue;
        QString line;
        if (!expand(raw, line) || !run(line.trimmed())) {
            fail(QStringLiteral("line %1: %2").arg(m_pc).arg(raw.trimmed()));
            return;
        }
        // One command per loop turn; a `sleep` defers the next by its wall-clock delay.
        QTimer::singleShot(m_sleepMs, this, &Driver::step);
        m_sleepMs = 0;
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
    if (cmd == QLatin1String("sleep")) {
        // Wall-clock pause with the event loop running — lets real timers fire (the
        // scene's interaction-idle timer, deferred rebuilds), which `settle` can't.
        if (!has(1))
            return false;
        m_sleepMs = std::max(0, static_cast<int>(num(1)));
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
    if (cmd == QLatin1String("fitnames")) {
        m_scene->fitNamesToTypical(); // the toolbar's "Fit names"
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
        // Always anchored at the view centre: CanvasView's AnchorUnderMouse reads the
        // real cursor (QCursor::pos), which a synthetic event can't place — and
        // offscreen there is no cursor at all. `zoom F X Y` is the anchored form.
        if (!has(1))
            return false;
        const int n = static_cast<int>(num(1));
        const QPoint p = vp->rect().center();
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
        // Deliver to the view itself: CanvasView's key handlers live there, and the
        // viewport would only pass a key on by ignoring it.
        m_view->setFocus();
        if (cmd == QLatin1String("key"))
            QTest::keyClick(m_view, key, mods);
        else if (cmd == QLatin1String("keydown"))
            QTest::keyPress(m_view, key, mods);
        else
            QTest::keyRelease(m_view, key, mods);
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
        // QTest::mouseMove after mousePress delivers a MouseMove carrying the held
        // button (Qt >= 6.3, pinned in CMake), so the scene's drag handlers see a real
        // drag. Several steps cross the drag threshold and give the handlers frames.
        constexpr int steps = 8;
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(vp, from + (to - from) * i / steps);
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
        else if (what == QLatin1String("deepen"))
            m_scene->setLazyDeepen(i != 0); // lazy deepening on/off (off = a fixed-depth control)
        else if (what == QLatin1String("fast"))
            m_scene->setInteracting(i != 0); // interaction LOD, held for benching
        else if (what == QLatin1String("focus"))
            m_scene->setLayoutFocusEnabled(i != 0); // layout focus (#40), off to compare
        else
            return false;
        return true;
    }
    if (cmd == QLatin1String("shot")) {
        if (!has(1))
            return false;
        vp->repaint(); // a fresh frame before the grab, even mid-script
        // `shot PATH view` grabs only the canvas viewport — the form a test should
        // assert on, since window chrome alone would satisfy `check nonblank`.
        const bool viewOnly = has(2) && a.at(2).toLower() == QLatin1String("view");
        const QPixmap pm = viewOnly ? vp->grab() : m_window->grab();
        if (!pm.save(a.at(1), "PNG")) {
            std::fprintf(stderr, "driver: cannot write %s\n", qPrintable(a.at(1)));
            return false;
        }
        std::printf("shot %s (%dx%d)\n", qPrintable(a.at(1)), pm.width(), pm.height());
        return true;
    }
    if (cmd == QLatin1String("check")) {
        if (!has(1))
            return false;
        const QString what = a.at(1).toLower();
        if (!has(2) && what != QLatin1String("grew"))
            return false; // every check but `grew` takes an argument
        if (what == QLatin1String("bases")) {
            // The number of base surfaces the scene holds — catches an unreadable
            // `load` path, which the window reports in a label rather than failing.
            const int want = static_cast<int>(num(2));
            const int got = static_cast<int>(m_scene->baseFrames().size());
            if (got != want) {
                std::fprintf(stderr, "driver: %d bases, expected %d\n", got, want);
                return false;
            }
            return true;
        }
        if (what == QLatin1String("file")) {
            // A file with this base name is selected (any directory).
            const QString want = a.at(2);
            bool found = false;
            for (const Selection::Entry &e : m_scene->selection().entries())
                if (e.name == want) // the projected name (urls() carry the on-disk origin)
                    found = true;
            if (!found) {
                std::fprintf(stderr, "driver: %s is not selected\n", qPrintable(want));
                return false;
            }
            return true;
        }
        if (what == QLatin1String("ops")) {
            const int got = m_scene->ledger().size();
            std::printf("ops %d\n", got);
            if (got != static_cast<int>(num(2))) {
                std::fprintf(stderr, "driver: %d ops staged, expected %d\n", got,
                             static_cast<int>(num(2)));
                return false;
            }
            return true;
        }
        if (what == QLatin1String("focus")) {
            // The layout focus (ADR-305) of the surface under the probe point is the
            // directory whose path ends with PATH, or `none`. A suffix so scripts stay
            // fixture-location independent.
            const core::FsNode *f = probedFocus();
            const QString got = f ? f->path : QStringLiteral("none");
            std::printf("focus %s\n", qPrintable(got));
            const QString want = a.at(2);
            bool ok = false;
            if (want == QLatin1String("none"))
                ok = f == nullptr;
            else if (want.contains(QLatin1Char('*'))) // a glob, for synth trees whose
                ok = f && QRegularExpression(         // sibling order follows readdir
                              QRegularExpression::wildcardToRegularExpression(
                                  want, QRegularExpression::NonPathWildcardConversion))
                              .match(f->path)
                              .hasMatch();
            else
                ok = f && f->path.endsWith(want);
            if (!ok) {
                std::fprintf(stderr, "driver: focus is %s, expected %s\n", qPrintable(got),
                             qPrintable(want));
                return false;
            }
            return true;
        }
        if (what == QLatin1String("focusdepth") || what == QLatin1String("focuspopped")) {
            // `check focusdepth N`: the layout focus (#40) sits N directories below
            // its base root (`N+` = at least N); `check focuspopped`: shallower than
            // at the last `mark`. Both need a focus.
            const int got = focusDepth();
            std::printf("focusdepth %d\n", got);
            if (got < 0) {
                std::fprintf(stderr, "driver: no layout focus\n");
                return false;
            }
            if (what == QLatin1String("focuspopped")) {
                if (m_markFocusDepth < 0 || got >= m_markFocusDepth) {
                    std::fprintf(stderr, "driver: focus depth %d, mark was %d\n", got,
                                 m_markFocusDepth);
                    return false;
                }
                return true;
            }
            const bool atLeast = a.at(2).endsWith(QLatin1Char('+'));
            const int want = static_cast<int>(num(2)); // toDouble stops at the '+'
            if (atLeast ? got < want : got != want) {
                std::fprintf(stderr, "driver: focus depth %d, expected %s\n", got,
                             qPrintable(a.at(2)));
                return false;
            }
            return true;
        }
        if (what == QLatin1String("selected")) {
            const int got = m_scene->selection().count();
            std::printf("selected %d\n", got);
            if (got != static_cast<int>(num(2))) {
                std::fprintf(stderr, "driver: %d selected, expected %d\n", got,
                             static_cast<int>(num(2)));
                return false;
            }
            return true;
        }
        if (what == QLatin1String("nodes") || what == QLatin1String("grew")) {
            // `check nodes N`: directories across every base's render tree ≥ N.
            // `check grew`: more than at the last `mark` — the lazy-deepening assertion
            // in fixture-independent form.
            const int got = countNodes();
            std::printf("nodes %d\n", got);
            if (what == QLatin1String("grew")) {
                if (m_mark < 0 || got <= m_mark) {
                    std::fprintf(stderr, "driver: %d nodes, mark was %d\n", got, m_mark);
                    return false;
                }
                return true;
            }
            const int want = static_cast<int>(num(2));
            if (got < want) {
                std::fprintf(stderr, "driver: %d nodes, expected at least %d\n", got, want);
                return false;
            }
            return true;
        }
        if (what == QLatin1String("nonblank")) {
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
        if (what == QLatin1String("differs") || what == QLatin1String("same")) {
            // `differs`: two captures must not be pixel-identical — a zoom or pan
            // between them changed what the canvas drew. `same`: they must be — the
            // assertion that nothing in between repainted differently.
            if (!has(3))
                return false;
            const QImage x(a.at(2)), y(a.at(3));
            if (x.isNull() || y.isNull())
                return false;
            const bool same = x == y;
            if (same != (what == QLatin1String("same"))) {
                std::fprintf(stderr, "driver: %s and %s are %s\n", qPrintable(a.at(2)),
                             qPrintable(a.at(3)), same ? "identical" : "different");
                return false;
            }
            return true;
        }
        return false;
    }
    if (cmd == QLatin1String("bench")) {
        // Synchronous repaints of the viewport, timed — the per-frame cost of the
        // current view state, which a sampling profiler over a whole run can't isolate
        // from scanning and I/O.
        const int n = has(1) ? std::max(1, static_cast<int>(num(1))) : 20;
        vp->repaint(); // warm-up: the first frame pays for cache population
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < n; ++i)
            vp->repaint();
        const double ms = static_cast<double>(t.nsecsElapsed()) / 1e6 / n;
        std::printf("bench %d frames: %.2f ms/frame\n", n, ms);
        std::fflush(stdout);
        return true;
    }
    if (cmd == QLatin1String("probe")) {
        // What is under a viewport point: the topmost surface's deepest cell — its
        // path, direct file count, child count, truncated flag. Diagnostic.
        const QPoint vp0 = viewPoint(1, 2);
        m_probe = vp0; // the focus checks ask this surface
        const QPointF scenePos = m_view->mapToScene(vp0);
        const core::FsNode *best = nullptr;
        qreal bestZ = -1e18;
        for (FrameItem *f : m_scene->frames()) { // bases and lenses: the topmost wins
            TreemapItem *t = f->interiorTreemap();
            if (!t)
                continue;
            const QPointF local = t->mapFromScene(scenePos);
            if (const core::FsNode *n = t->cellNodeAt(local); n && f->zValue() >= bestZ) {
                best = n;
                bestZ = f->zValue();
            }
        }
        if (!best) {
            std::printf("probe (%d,%d): nothing\n", vp0.x(), vp0.y());
            return true;
        }
        // The file glyph under the point too, if any (topmost surface).
        for (FrameItem *f : m_scene->frames())
            if (TreemapItem *t = f->interiorTreemap(); t && f->zValue() >= bestZ)
                if (const auto [dir, idx] = t->fileAt(t->mapFromScene(scenePos)); dir) {
                    std::printf("probe (%d,%d): file %s/%s [%d]\n", vp0.x(), vp0.y(),
                                qPrintable(dir->path),
                                qPrintable(dir->files[static_cast<std::size_t>(idx)].name), idx);
                    break;
                }
        std::printf("probe (%d,%d): %s files=%zu children=%zu truncated=%d lazy=%d\n", vp0.x(),
                    vp0.y(), qPrintable(best->path), best->files.size(), best->children.size(),
                    best->truncatedDepth ? 1 : 0, best->lazyChildren ? 1 : 0);
        const core::FsNode *focus = probedFocus();
        std::printf("probe focus: %s\n", focus ? qPrintable(focus->path) : "none");
        std::fflush(stdout);
        return true;
    }
    if (cmd == QLatin1String("stage")) {
        // Stage a file op on the glyph under a viewport point, bypassing the menu
        // (which is modal): `stage rename X Y NEW`, `stage trash X Y`,
        // `stage move X Y DX DY` (the file at (X,Y) into the directory cell at (DX,DY)).
        // The subject is either the glyph at (X,Y), or `@NAME`: the file named NAME in
        // the leaf cell under the viewport centre — geometry-free, so a script can
        // keep staging after an earlier op re-flowed the map.
        if (!has(2))
            return false;
        const QString what = a.at(1).toLower();
        const bool byName = a.at(2).startsWith(QLatin1Char('@'));
        int argi = byName ? 3 : 4; // index of the first argument after the subject
        QString name;
        const core::FsNode *dir = nullptr;
        if (byName) {
            // The first laid-out leaf cell (any surface) holding a file of that name.
            name = a.at(2).mid(1);
            for (FrameItem *f : m_scene->frames()) {
                TreemapItem *t = f->interiorTreemap();
                if (!t || dir)
                    continue;
                for (const LayoutCell &c : t->layout().cells()) {
                    if (c.subdivided)
                        continue;
                    for (const auto &fe : c.node->files)
                        if (fe.name == name) {
                            dir = c.node;
                            break;
                        }
                    if (dir)
                        break;
                }
            }
            if (!dir) {
                std::fprintf(stderr, "driver: no laid-out cell holds a file named %s\n",
                             qPrintable(name));
                return false;
            }
        } else {
            if (!has(3))
                return false;
            const QPoint at(static_cast<int>(num(2)), static_cast<int>(num(3)));
            const QPointF scenePos = m_view->mapToScene(at);
            int idx = -1;
            for (FrameItem *f : m_scene->frames())
                if (TreemapItem *t = f->interiorTreemap())
                    if (const auto hit = t->fileAt(t->mapFromScene(scenePos)); hit.first) {
                        dir = hit.first;
                        idx = hit.second;
                    }
            if (!dir) {
                std::fprintf(stderr, "driver: no file glyph at (%d,%d)\n", at.x(), at.y());
                return false;
            }
            name = dir->files[static_cast<std::size_t>(idx)].name;
        }
        const core::MemberKey key = core::keyFor(*dir);
        bool ok = false;
        if (what == QLatin1String("rename") && has(argi))
            ok = m_scene->stageRename(key, name, a.at(argi));
        else if (what == QLatin1String("trash"))
            ok = m_scene->stageTrash(key, name);
        else if (what == QLatin1String("move") && has(argi + 1)) {
            const QPoint to(static_cast<int>(num(argi)), static_cast<int>(num(argi + 1)));
            const QPointF toScene = m_view->mapToScene(to);
            const core::FsNode *dest = nullptr;
            for (FrameItem *f : m_scene->frames())
                if (TreemapItem *t = f->interiorTreemap())
                    if (const core::FsNode *n = t->cellNodeAt(t->mapFromScene(toScene)))
                        dest = n;
            ok = m_scene->stageMoveFile(key, name, dest);
        } else
            return false;
        // A refusal (illegal or unresolved) is a legitimate outcome — `check ops N`
        // asserts what was staged.
        std::printf("stage %s %s: %s\n", qPrintable(what), qPrintable(name),
                    ok ? "staged" : "refused");
        std::fflush(stdout);
        m_settle = 3; // the re-projection is deferred a turn
        return true;
    }
    if (cmd == QLatin1String("mark")) {
        m_mark = countNodes();           // for a later `check grew`
        m_markFocusDepth = focusDepth(); // for a later `check focuspopped`
        std::printf("mark %d\n", m_mark);
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

const core::FsNode *Driver::probedFocus() const {
    const QPoint p = m_probe.x() >= 0 ? m_probe : m_view->viewport()->rect().center();
    return m_scene->layoutFocusAt(m_view->mapToScene(p));
}

// Directories between the probed surface's layout focus (ADR-305) and the root of
// the base it lives in (0 = a base root, -1 = no focus or not under any base).
int Driver::focusDepth() const {
    const core::FsNode *f = probedFocus();
    if (!f)
        return -1;
    for (FrameItem *b : m_scene->baseFrames()) {
        const QString base = b->sourceRoot()->path;
        if (f->path == base)
            return 0;
        if (f->path.startsWith(base + QLatin1Char('/')))
            return static_cast<int>(f->path.mid(base.size() + 1).count(QLatin1Char('/'))) + 1;
    }
    return -1;
}

int Driver::countNodes() const {
    int got = 0;
    std::function<void(const core::FsNode *)> count = [&](const core::FsNode *n) {
        if (!n)
            return;
        ++got;
        for (const auto &c : n->children)
            count(c.get());
    };
    for (FrameItem *f : m_scene->baseFrames())
        count(f->node());
    return got;
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
