// Unit tests for group-store JSON persistence (ADR-102 / task #15): the save/load
// round-trip of core::GroupStore. Runs with QStandardPaths test mode enabled so the
// sidecar lands in a throwaway temp dir, never the real ~/.local/share. Plain assert
// harness registered with ctest, matching tests/move_test.cpp.

#include "core/group.h"
#include "core/groupstore_io.h"

#include <QColor>
#include <QFile>
#include <QStandardPaths>

#include <cstdio>

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

} // namespace

int main() {
    QStandardPaths::setTestModeEnabled(true); // redirect the data dir to a temp location
    testRoundTrip();
    testMissingAndDeterminism();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all groupstore_io checks passed\n");
    return 0;
}
