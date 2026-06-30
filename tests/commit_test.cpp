// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the commit-engine dry-run verification (ADR-200 / task #16a):
// core::verifyPlan. The on-disk stat is injected, so every verdict — OK, drift, missing,
// cross-volume, illegal, unresolved — is reachable without touching a real filesystem.
// Plain assert harness registered with ctest, matching tests/move_test.cpp.

#include "core/commit.h"
#include "core/fsnode.h"
#include "core/move.h"

#include <QHash>
#include <QString>

#include <cstdio>
#include <memory>
#include <vector>

using core::FsNode;
using core::MoveOp;
using core::VerifyStatus;

namespace {

int g_failures = 0;

void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

core::Fingerprint fp(quint64 dev, quint64 ino) {
    core::Fingerprint f;
    f.dev = dev;
    f.ino = ino;
    f.valid = true;
    return f;
}

// A child dir with its own (dev, inode) fingerprint, mirroring what the scanner records.
FsNode *addChild(FsNode *parent, const QString &name, quint64 dev, quint64 ino) {
    auto c = std::make_unique<FsNode>();
    c->name = name;
    c->path = parent->path + QLatin1Char('/') + name;
    c->originalPath = c->path;
    c->parent = parent;
    c->fp = fp(dev, ino);
    FsNode *raw = c.get();
    parent->children.push_back(std::move(c));
    return raw;
}

std::unique_ptr<FsNode> makeRoot(const QString &path, quint64 dev, quint64 ino) {
    auto r = std::make_unique<FsNode>();
    r->path = path;
    r->name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    r->originalPath = path;
    r->fp = fp(dev, ino);
    return r;
}

MoveOp mv(const QString &src, const QString &dst, const QString &name) {
    return MoveOp{src, dst, name};
}

// A disk view that matches a tree exactly (every node present with its own fingerprint).
void fillDisk(const FsNode *n, QHash<QString, core::Fingerprint> &disk) {
    disk.insert(n->originalPath.isEmpty() ? n->path : n->originalPath, n->fp);
    for (const auto &c : n->children)
        fillDisk(c.get(), disk);
}

VerifyStatus statusOf(const core::CommitPlan &p, int i) {
    return p.ops[static_cast<size_t>(i)].status;
}

void run() {
    // /r ── a ── leaf      (a, leaf, b on device 1; c on device 2)
    //    ├─ b
    //    └─ c   (other volume)
    auto root = std::make_unique<FsNode>();
    root->path = QStringLiteral("/r");
    root->name = QStringLiteral("r");
    root->originalPath = root->path;
    root->fp = fp(1, 1);
    FsNode *a = addChild(root.get(), QStringLiteral("a"), 1, 10);
    FsNode *leaf = addChild(a, QStringLiteral("leaf"), 1, 11);
    addChild(root.get(), QStringLiteral("b"), 1, 20);
    addChild(root.get(), QStringLiteral("c"), 2, 30);
    (void)leaf;

    // The injected disk view: by default everything matches what we scanned.
    QHash<QString, core::Fingerprint> disk;
    disk.insert(QStringLiteral("/r/a/leaf"), fp(1, 11));
    disk.insert(QStringLiteral("/r/a"), fp(1, 10));
    disk.insert(QStringLiteral("/r/b"), fp(1, 20));
    disk.insert(QStringLiteral("/r/c"), fp(2, 30));
    auto statOf = [&](const QString &p) { return disk.value(p, core::Fingerprint{}); };

    const std::vector<const FsNode *> roots = {root.get()};

    // OK: leaf → b, source present and unchanged, same volume.
    {
        auto plan = core::verifyPlan(roots, {mv("/r/a/leaf", "/r/b", "leaf")}, statOf);
        check(statusOf(plan, 0) == VerifyStatus::Ok, "verify: legal in-volume move is OK");
        check(plan.allClear() && plan.okCount() == 1, "verify: allClear/okCount for a clean plan");
    }
    // Unresolved: a key that isn't in the scanned forest.
    {
        auto plan = core::verifyPlan(roots, {mv("/r/ghost", "/r/b", "ghost")}, statOf);
        check(statusOf(plan, 0) == VerifyStatus::Unresolved, "verify: missing key is Unresolved");
    }
    // IllegalMove: cycle (a into its own descendant leaf).
    {
        auto plan = core::verifyPlan(roots, {mv("/r/a", "/r/a/leaf", "a")}, statOf);
        check(statusOf(plan, 0) == VerifyStatus::IllegalMove, "verify: cycle is IllegalMove");
        check(plan.ops[0].legality == core::MoveLegality::Cycle, "verify: legality detail is Cycle");
    }
    // SourceMissing: the source no longer exists on disk.
    {
        QHash<QString, core::Fingerprint> gone = disk;
        gone.remove(QStringLiteral("/r/a/leaf"));
        auto statGone = [&](const QString &p) { return gone.value(p, core::Fingerprint{}); };
        auto plan = core::verifyPlan(roots, {mv("/r/a/leaf", "/r/b", "leaf")}, statGone);
        check(statusOf(plan, 0) == VerifyStatus::SourceMissing, "verify: absent source is SourceMissing");
    }
    // SourceDrifted: a different inode now occupies the source path.
    {
        QHash<QString, core::Fingerprint> drift = disk;
        drift.insert(QStringLiteral("/r/a/leaf"), fp(1, 999)); // recycled / replaced
        auto statDrift = [&](const QString &p) { return drift.value(p, core::Fingerprint{}); };
        auto plan = core::verifyPlan(roots, {mv("/r/a/leaf", "/r/b", "leaf")}, statDrift);
        check(statusOf(plan, 0) == VerifyStatus::SourceDrifted, "verify: changed inode is SourceDrifted");
    }
    // CrossVolume: leaf (dev 1) → c (dev 2).
    {
        auto plan = core::verifyPlan(roots, {mv("/r/a/leaf", "/r/c", "leaf")}, statOf);
        check(statusOf(plan, 0) == VerifyStatus::CrossVolume, "verify: cross-device move is CrossVolume");
    }
    // A mixed plan of independent ops: counts split correctly.
    {
        auto plan = core::verifyPlan(roots,
                                     {mv("/r/a/leaf", "/r/b", "leaf"), // OK (dev1 → dev1)
                                      mv("/r/ghost", "/r/b", "ghost"), // unresolved
                                      mv("/r/b", "/r/c", "b")},        // cross-volume (dev1 → dev2)
                                     statOf);
        check(!plan.allClear(), "verify: mixed plan is not all-clear");
        check(plan.okCount() == 1 && plan.blockedCount() == 2, "verify: ok/blocked counts");
    }
}

// A chained plan: op B is illegal against the static base (its destination still holds a
// colliding name) but legal once op A vacates that name. Verifier must judge in apply order.
void runChained() {
    auto w = makeRoot(QStringLiteral("/w"), 1, 1);
    FsNode *x = addChild(w.get(), QStringLiteral("x"), 1, 10);
    addChild(x, QStringLiteral("item"), 1, 11);
    FsNode *y = addChild(w.get(), QStringLiteral("y"), 1, 20);
    addChild(y, QStringLiteral("item"), 1, 21); // y already holds "item" → base collision
    addChild(w.get(), QStringLiteral("z"), 1, 30);
    QHash<QString, core::Fingerprint> disk;
    fillDisk(w.get(), disk);
    auto statOf = [&](const QString &p) { return disk.value(p, core::Fingerprint{}); };
    const std::vector<const FsNode *> roots = {w.get()};

    // Alone, moving x/item into y collides with y/item.
    auto solo = core::verifyPlan(roots, {mv("/w/x/item", "/w/y", "item")}, statOf);
    check(statusOf(solo, 0) == VerifyStatus::IllegalMove, "chained: op is a collision against base");
    check(solo.ops[0].legality == core::MoveLegality::Collision, "chained: collision detail");

    // Vacate y/item to z first, then x/item → y is legal in apply order.
    auto chained = core::verifyPlan(
        roots, {mv("/w/y/item", "/w/z", "item"), mv("/w/x/item", "/w/y", "item")}, statOf);
    check(statusOf(chained, 0) == VerifyStatus::Ok, "chained: op A vacates the name");
    check(statusOf(chained, 1) == VerifyStatus::Ok,
          "chained: op B legal after A cleared the collision (evolving-tree, not base)");
}

// The destination parent vanished on disk between scan and verify → DestMissing, not a
// false OK.
void runDestMissing() {
    auto w = makeRoot(QStringLiteral("/w2"), 1, 1);
    FsNode *x = addChild(w.get(), QStringLiteral("x"), 1, 10);
    addChild(x, QStringLiteral("item"), 1, 11);
    addChild(w.get(), QStringLiteral("z"), 1, 30); // the destination
    QHash<QString, core::Fingerprint> disk;
    fillDisk(w.get(), disk);
    disk.remove(QStringLiteral("/w2/z")); // destination gone on disk
    auto statOf = [&](const QString &p) { return disk.value(p, core::Fingerprint{}); };

    auto plan = core::verifyPlan({w.get()}, {mv("/w2/x/item", "/w2/z", "item")}, statOf);
    check(statusOf(plan, 0) == VerifyStatus::DestMissing, "dest: vanished destination is DestMissing");
}

} // namespace

int main() {
    run();
    runChained();
    runDestMissing();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all commit checks passed\n");
    return 0;
}
