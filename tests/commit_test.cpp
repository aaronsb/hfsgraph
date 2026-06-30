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

MoveOp mv(const QString &src, const QString &dst, const QString &name) {
    return MoveOp{src, dst, name};
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
    // A mixed plan: counts split correctly.
    {
        auto plan = core::verifyPlan(roots,
                                     {mv("/r/a/leaf", "/r/b", "leaf"),   // OK
                                      mv("/r/a", "/r/a/leaf", "a"),       // illegal (cycle)
                                      mv("/r/ghost", "/r/b", "ghost")},   // unresolved
                                     statOf);
        check(!plan.allClear(), "verify: mixed plan is not all-clear");
        check(plan.okCount() == 1 && plan.blockedCount() == 2, "verify: ok/blocked counts");
    }
}

} // namespace

int main() {
    run();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all commit checks passed\n");
    return 0;
}
