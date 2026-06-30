// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for group-store JSON persistence (ADR-102 / task #15): the save/load
// round-trip of core::GroupStore. Runs with QStandardPaths test mode enabled so the
// sidecar lands in a throwaway temp dir, never the real ~/.local/share. Plain assert
// harness registered with ctest, matching tests/move_test.cpp.

#include "core/fsnode.h"
#include "core/group.h"
#include "core/groupstore_io.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdio>
#include <memory>

using core::Group;
using core::GroupKind;
using core::GroupStore;

namespace {

int g_failures = 0;

void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

const Group *byName(const GroupStore &s, const QString &name) {
    for (const auto &g : s.groups())
        if (g->name == name)
            return g.get();
    return nullptr;
}

void testRoundTrip() {
    const QString root = QStringLiteral("/home/u/Projects");

    GroupStore src;
    // A rule group with an anchor, an exclusion, a non-default colour, and view state.
    Group *rule = src.create(GroupKind::Rule, QStringLiteral("myrepo"), QColor(QStringLiteral("#4d96e6")));
    rule->rule = core::GroupRule::GitWorktree;
    rule->ruleAnchor = QStringLiteral("/home/u/Projects/myrepo");
    rule->members.insert(QStringLiteral("/home/u/Projects/myrepo"));  // transient — not persisted
    rule->exclusions.insert(QStringLiteral("/home/u/Projects/myrepo/node_modules"));
    rule->view.highlight = true;
    rule->view.focus = true;
    // A manual group with a curated member set.
    Group *manual = src.create(GroupKind::Manual, QStringLiteral("photos"), QColor(QStringLiteral("#5cb85c")));
    manual->members.insert(QStringLiteral("/home/u/Projects/a"));
    manual->members.insert(QStringLiteral("/home/u/Projects/b"));
    manual->view.dim = true;

    check(core::saveGroupStore(src, root, QStringLiteral("ws-uuid-1")), "save: writes the sidecar");
    check(QFile::exists(core::workspaceStorePath(root)), "save: sidecar file exists on disk");

    GroupStore dst;
    check(core::loadGroupStore(dst, root), "load: reads the sidecar back");
    check(dst.groups().size() == 2, "load: both groups restored");

    const Group *r = byName(dst, QStringLiteral("myrepo"));
    check(r != nullptr, "load: rule group present");
    check(r && r->kind == GroupKind::Rule, "load: kind round-trips (rule)");
    check(r && r->color.name() == QStringLiteral("#4d96e6"), "load: colour round-trips");
    check(r && r->ruleAnchor == QStringLiteral("/home/u/Projects/myrepo"), "load: ruleAnchor round-trips");
    check(r && r->members.isEmpty(), "load: rule members NOT persisted (recomputed on resolve)");
    check(r && r->exclusions.contains(QStringLiteral("/home/u/Projects/myrepo/node_modules")),
          "load: exclusion round-trips");
    check(r && r->view.highlight && r->view.focus && !r->view.dim, "load: rule view state round-trips");

    const Group *m = byName(dst, QStringLiteral("photos"));
    check(m != nullptr, "load: manual group present");
    check(m && m->kind == GroupKind::Manual, "load: kind round-trips (manual)");
    check(m && m->members.size() == 2, "load: manual members persisted");
    check(m && m->members.contains(QStringLiteral("/home/u/Projects/a")), "load: manual member a present");
    check(m && m->view.dim, "load: manual view state round-trips");

    // adopt() must keep ids and advance the counter so a later create() can't collide.
    check(r && r->id == QStringLiteral("g1") && m && m->id == QStringLiteral("g2"), "load: ids preserved");
    Group *fresh = dst.create(GroupKind::Manual, QStringLiteral("new"), QColor());
    check(fresh && fresh->id != QStringLiteral("g1") && fresh->id != QStringLiteral("g2"),
          "load: create() after load yields a non-colliding id");
}

void testMissingAndDeterminism() {
    check(core::workspaceStorePath(QStringLiteral("/x")) == core::workspaceStorePath(QStringLiteral("/x")),
          "path: deterministic for a given root");
    check(core::workspaceStorePath(QStringLiteral("/x")) != core::workspaceStorePath(QStringLiteral("/y")),
          "path: differs for different roots");

    GroupStore s;
    check(!core::loadGroupStore(s, QStringLiteral("/never/saved/here")), "load: false when no sidecar");
    check(s.empty(), "load: store untouched when nothing to load");
}

// The headline behaviour (review #5): load a persisted rule group, then resolveRuleGroups
// against a tree — it must reconcile by anchor into ONE group with the persisted colour/
// view/exclusions preserved and members re-resolved, not spawn a default-coloured duplicate.
void testLoadReconcile() {
    const QString root = QStringLiteral("/ws-reconcile");
    {
        GroupStore s;
        Group *g = s.create(GroupKind::Rule, QStringLiteral("repo"), QColor(QStringLiteral("#123456")));
        g->ruleAnchor = QStringLiteral("/wsr/repo");
        g->exclusions.insert(QStringLiteral("/wsr/repo/.git"));
        g->view.focus = true;
        check(core::saveGroupStore(s, root, QString()), "reconcile: persisted a rule group");
    }
    // A tree where /wsr/repo is a git-worktree anchor (it holds a `.git` child).
    core::FsNode wsr;
    wsr.path = QStringLiteral("/wsr");
    wsr.name = QStringLiteral("wsr");
    auto repo = std::make_unique<core::FsNode>();
    repo->path = QStringLiteral("/wsr/repo");
    repo->name = QStringLiteral("repo");
    repo->parent = &wsr;
    for (const char *child : {".git", "src"}) {
        auto c = std::make_unique<core::FsNode>();
        c->name = QString::fromLatin1(child);
        c->path = repo->path + QLatin1Char('/') + c->name;
        c->parent = repo.get();
        repo->children.push_back(std::move(c));
    }
    wsr.children.push_back(std::move(repo));

    GroupStore loaded;
    check(core::loadGroupStore(loaded, root), "reconcile: loaded the sidecar");
    core::resolveRuleGroups({&wsr}, loaded);

    check(loaded.groups().size() == 1, "reconcile: one group, no default-coloured duplicate");
    const Group *g = loaded.groups().empty() ? nullptr : loaded.groups().front().get();
    check(g && g->color.name() == QStringLiteral("#123456"), "reconcile: persisted colour preserved");
    check(g && g->view.focus, "reconcile: persisted view preserved");
    check(g && g->members.contains(QStringLiteral("/wsr/repo/src")), "reconcile: members re-resolved");
    check(g && g->exclusions.contains(QStringLiteral("/wsr/repo/.git")), "reconcile: exclusion preserved");
}

// Review HIGH #1: a save from a store that has *dropped* a group (e.g. a shallow scan
// reconciled it away) must not erase that group from the on-disk sidecar.
void testMergePreserve() {
    const QString root = QStringLiteral("/ws-merge");
    GroupStore a;
    Group *g1 = a.create(GroupKind::Manual, QStringLiteral("one"), QColor(QStringLiteral("#111111")));
    g1->members.insert(QStringLiteral("/m/a"));
    a.create(GroupKind::Manual, QStringLiteral("two"), QColor(QStringLiteral("#222222")));
    check(core::saveGroupStore(a, root, QString()), "merge: initial save (two groups)");

    GroupStore b; // only "one" survives in memory (same id g1), with a changed colour
    Group *g1b = b.create(GroupKind::Manual, QStringLiteral("one"), QColor(QStringLiteral("#abcdef")));
    g1b->members.insert(QStringLiteral("/m/a"));
    check(g1b->id == QStringLiteral("g1"), "merge: live group shares the original id");
    check(core::saveGroupStore(b, root, QString()), "merge: second save (only g1 in memory)");

    GroupStore c;
    core::loadGroupStore(c, root);
    check(c.groups().size() == 2, "merge: dropped group preserved on disk (no data loss)");
    const Group *two = byName(c, QStringLiteral("two"));
    const Group *one = byName(c, QStringLiteral("one"));
    check(one && one->color.name() == QStringLiteral("#abcdef"), "merge: live group overwritten with new state");
    check(two && two->color.name() == QStringLiteral("#222222"), "merge: dropped group kept its state");

    // Loading again into the same store is additive-by-id: no duplicates (the reload-on-
    // every-resolve flow relies on this idempotence).
    core::loadGroupStore(c, root);
    check(c.groups().size() == 2, "merge: re-load into a populated store doesn't duplicate");
}

// Review MEDIUM #3: a sidecar written by a newer format must be refused, not mis-parsed.
void testVersionGuard() {
    const QString root = QStringLiteral("/ws-version");
    const QString p = core::workspaceStorePath(root);
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    check(f.open(QIODevice::WriteOnly), "version: wrote a future-version sidecar");
    f.write("{\"version\": 999, \"groups\": []}");
    f.close();
    GroupStore s;
    check(!core::loadGroupStore(s, root), "version: refuses a newer format");
}

} // namespace

int main() {
    QStandardPaths::setTestModeEnabled(true); // redirect the data dir to a temp location
    testRoundTrip();
    testMissingAndDeterminism();
    testLoadReconcile();
    testMergePreserve();
    testVersionGuard();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all groupstore_io checks passed\n");
    return 0;
}
