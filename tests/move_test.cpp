// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the move-staging model (ADR-302 / task #9): core::Ledger editing
// semantics and core::projectForest replay. Pure core, no Qt-GUI — a plain assert
// harness registered with ctest (no extra test-framework dependency).

#include "core/fsnode.h"
#include "core/move.h"

#include <QString>

#include <cstdio>
#include <vector>

using core::checkFileOp;
using core::FileEntry;
using core::FsNode;
using core::Ledger;
using core::MoveLegality;
using core::MoveOp;
using core::OpKind;
using core::replayLegality;

namespace {

int g_failures = 0;

void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// Build a child directory under `parent` with a derived path, returning a raw
// pointer (ownership stays in the parent).
FsNode *addChild(FsNode *parent, const QString &name, int ownFiles = 0) {
    auto c = std::make_unique<FsNode>();
    c->name = name;
    c->path = parent->path + QLatin1Char('/') + name;
    c->originalPath = c->path; // the scanner sets this; the diff overlay reads it
    c->parent = parent;
    c->fileCount = ownFiles;
    FsNode *raw = c.get();
    parent->children.push_back(std::move(c));
    return raw;
}

std::unique_ptr<FsNode> makeRoot(const QString &path) {
    auto r = std::make_unique<FsNode>();
    r->path = path;
    r->originalPath = path;
    r->name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    return r;
}

// Find a direct child by name (nullptr if absent).
const FsNode *child(const FsNode *n, const QString &name) {
    for (const auto &c : n->children)
        if (c->name == name)
            return c.get();
    return nullptr;
}

MoveOp mv(const QString &src, const QString &dst) {
    return MoveOp{src, dst, QString()};
}

// A file entry in `dir` (the scanner populates these; file ops act on them).
void addFile(FsNode *dir, const QString &name, qint64 bytes = 1) {
    FileEntry fe;
    fe.name = name;
    fe.sizeBytes = bytes;
    dir->files.push_back(fe);
    dir->fileCount += 1;
    dir->sizeBytes += bytes;
}

bool hasFile(const FsNode *dir, const QString &name) {
    for (const auto &fe : dir->files)
        if (fe.name == name)
            return true;
    return false;
}

MoveOp fileOp(OpKind kind, const QString &dir, const QString &name, const QString &to = QString()) {
    MoveOp op;
    op.kind = kind;
    op.source = dir;
    op.fileName = name;
    op.sourceName = name;
    if (kind == OpKind::MoveFile)
        op.destParent = to;
    else if (kind == OpKind::RenameFile)
        op.newName = to;
    return op;
}

void testLedger() {
    Ledger l;
    check(l.empty() && l.size() == 0 && l.step() == 0, "ledger starts empty");

    l.append(mv("/r/a", "/r/b"));
    l.append(mv("/r/c", "/r/d"));
    check(l.size() == 2 && l.step() == 2, "append advances step to end");
    check(l.active().size() == 2, "active = all when step at end");

    l.setStep(1);
    check(l.step() == 1 && l.active().size() == 1, "setStep scrubs the preview");
    l.setStep(99);
    check(l.step() == 2, "setStep clamps high");
    l.setStep(-5);
    check(l.step() == 0, "setStep clamps low");

    l.setStep(2); // scrub back to the end before testing undo's clamp
    check(l.undo() && l.size() == 1 && l.canRedo(), "undo pops the tail");
    check(l.step() == 1, "undo clamps step into range");
    check(l.redo() && l.size() == 2 && l.step() == 2, "redo restores the tail");

    l.undo();
    l.append(mv("/r/e", "/r/f")); // a fresh edit invalidates redo
    check(!l.canRedo(), "append clears redo history");

    l.clear();
    check(l.empty() && l.step() == 0 && !l.canUndo(), "clear resets the ledger");
}

void testIdentity() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    addChild(a, QStringLiteral("a1"));
    addChild(root.get(), QStringLiteral("b"));

    const std::vector<const FsNode *> roots = {root.get()};
    auto proj = core::projectForest(roots, {});
    check(proj.size() == 1 && proj[0], "identity: one projected root");
    check(proj[0]->children.size() == 2, "identity: top children preserved");
    check(child(proj[0].get(), QStringLiteral("a")) != nullptr, "identity: 'a' present");
    check(child(child(proj[0].get(), QStringLiteral("a")), QStringLiteral("a1")) != nullptr,
          "identity: nested 'a/a1' present");
    check(proj[0].get() != root.get(), "identity: projection is a copy, not the source");
}

void testMove() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    FsNode *leaf = addChild(a, QStringLiteral("leaf"));
    FsNode *b = addChild(root.get(), QStringLiteral("b"));
    (void)leaf;
    (void)b;

    // Move /r/a/leaf under /r/b.
    auto proj = core::projectForest({root.get()}, {mv("/r/a/leaf", "/r/b")});
    const FsNode *pa = child(proj[0].get(), QStringLiteral("a"));
    const FsNode *pb = child(proj[0].get(), QStringLiteral("b"));
    check(child(pa, QStringLiteral("leaf")) == nullptr, "move: leaf left its old parent");
    const FsNode *moved = child(pb, QStringLiteral("leaf"));
    check(moved != nullptr, "move: leaf arrived under new parent");
    check(moved && moved->path == QStringLiteral("/r/b/leaf"), "move: path recomputed");
    check(moved && moved->parent == pb, "move: parent pointer updated");
    // The diff overlay's "did it actually relocate?" contract (ADR-302 #12 / ADR-100):
    // a moved node's path diverges from where it was scanned, an unmoved one's does not.
    check(moved && moved->originalPath == QStringLiteral("/r/a/leaf"),
          "move: originalPath holds the scanned location");
    check(moved && moved->path != moved->originalPath, "move: relocated → path != originalPath");
    check(pa && pa->path == pa->originalPath, "move: untouched sibling → path == originalPath");
}

void testCycleAndCollision() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    addChild(a, QStringLiteral("a1"));

    // Cycle: move /r/a under its own child /r/a/a1 → skipped (a stays put).
    auto p1 = core::projectForest({root.get()}, {mv("/r/a", "/r/a/a1")});
    check(child(p1[0].get(), QStringLiteral("a")) != nullptr, "cycle: source unchanged");

    // Collision: /r/b already holds a child named 'a'; moving /r/a under /r/b skips.
    auto root2 = makeRoot(QStringLiteral("/r"));
    addChild(root2.get(), QStringLiteral("a"));
    FsNode *bb = addChild(root2.get(), QStringLiteral("b"));
    addChild(bb, QStringLiteral("a")); // collides with the top-level 'a'
    auto p2 = core::projectForest({root2.get()}, {mv("/r/a", "/r/b")});
    check(child(p2[0].get(), QStringLiteral("a")) != nullptr, "collision: source unchanged");
    check(p2[0]->children.size() == 2, "collision: no node added/removed at top");
}

void testRootAndUnresolved() {
    auto root = makeRoot(QStringLiteral("/r"));
    addChild(root.get(), QStringLiteral("a"));

    // Moving a root (no parent) and an unresolved key are both skipped.
    auto proj = core::projectForest({root.get()},
                                    {mv("/r", "/r/a"), mv("/nope", "/r/a"), mv("/r/a", "/nope")});
    check(proj[0] && proj[0]->children.size() == 1, "root/unresolved ops are no-ops");
    check(child(proj[0].get(), QStringLiteral("a")) != nullptr, "tree intact after skips");
}

void testCrossRoot() {
    auto r0 = makeRoot(QStringLiteral("/v0"));
    FsNode *x = addChild(r0.get(), QStringLiteral("x"));
    (void)x;
    auto r1 = makeRoot(QStringLiteral("/v1"));
    addChild(r1.get(), QStringLiteral("y"));

    // Move /v0/x into /v1 — a cross-surface move (ADR-304 ledger spans bases).
    auto proj = core::projectForest({r0.get(), r1.get()}, {mv("/v0/x", "/v1")});
    check(proj.size() == 2, "cross-root: two projected roots");
    check(child(proj[0].get(), QStringLiteral("x")) == nullptr, "cross-root: left source base");
    const FsNode *moved = child(proj[1].get(), QStringLiteral("x"));
    check(moved != nullptr, "cross-root: arrived in target base");
    check(moved && moved->path == QStringLiteral("/v1/x"), "cross-root: path rebased");
}

void testDuplicateOpIdempotent() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    addChild(a, QStringLiteral("leaf"));
    addChild(root.get(), QStringLiteral("b"));

    // The same move twice: the second is a collision against the just-moved node →
    // skipped, so replaying it again is a no-op (ADR-200 idempotent replay).
    auto proj =
        core::projectForest({root.get()}, {mv("/r/a/leaf", "/r/b"), mv("/r/a/leaf", "/r/b")});
    const FsNode *pb = child(proj[0].get(), QStringLiteral("b"));
    check(pb && pb->children.size() == 1, "duplicate op applied once");
}

void testCheckMove() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    FsNode *a1 = addChild(a, QStringLiteral("a1"));
    FsNode *b = addChild(root.get(), QStringLiteral("b"));
    addChild(b, QStringLiteral("a")); // b already holds a child named 'a'

    using core::checkMove;
    using core::MoveLegality;
    check(checkMove(a1, b) == MoveLegality::Ok, "checkMove: a1 under b is legal");
    check(checkMove(a, a) == MoveLegality::SameNode, "checkMove: self is SameNode");
    check(checkMove(nullptr, b) == MoveLegality::SameNode, "checkMove: null is SameNode");
    check(checkMove(root.get(), a) == MoveLegality::SourceIsRoot, "checkMove: root has no parent");
    check(checkMove(a, a1) == MoveLegality::Cycle, "checkMove: into own descendant is a cycle");
    check(checkMove(a, b) == MoveLegality::Collision, "checkMove: name clash at dest");
    check(checkMove(a, root.get()) == MoveLegality::Collision, "checkMove: drop onto own parent");
}

// The gesture captures move keys from *projected* nodes; identity must survive an
// earlier move so re-moving an already-moved node resolves (without it, keyFor would
// return the recomputed path and the op would silently no-op).
void testChainedMoveIdentity() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    addChild(a, QStringLiteral("leaf"));
    addChild(root.get(), QStringLiteral("b"));
    addChild(root.get(), QStringLiteral("c"));

    auto proj1 = core::projectForest({root.get()}, {mv("/r/a/leaf", "/r/b")});
    const FsNode *movedLeaf =
        child(child(proj1[0].get(), QStringLiteral("b")), QStringLiteral("leaf"));
    check(movedLeaf && movedLeaf->path == QStringLiteral("/r/b/leaf"),
          "chained: first move landed");
    // keyFor reads identity, pinned to the original key — not the recomputed path.
    const core::MemberKey leafKey = core::keyFor(*movedLeaf);
    check(leafKey == QStringLiteral("/r/a/leaf"), "chained: identity survives the move");
    const core::MemberKey cKey = core::keyFor(*child(proj1[0].get(), QStringLiteral("c")));

    auto proj2 = core::projectForest(
        {root.get()}, {mv("/r/a/leaf", "/r/b"), MoveOp{leafKey, cKey, QStringLiteral("leaf")}});
    check(child(child(proj2[0].get(), QStringLiteral("c")), QStringLiteral("leaf")) != nullptr,
          "chained: re-moved node re-resolves via identity");
    check(child(child(proj2[0].get(), QStringLiteral("b")), QStringLiteral("leaf")) == nullptr,
          "chained: left the intermediate parent");
}

} // namespace

// File ops (#38): rename / trash / move-file replay on the projection, with the
// directory counts following, and their legality verdicts.
void testFileOps() {
    auto root = makeRoot(QStringLiteral("/r"));
    FsNode *a = addChild(root.get(), QStringLiteral("a"));
    FsNode *b = addChild(root.get(), QStringLiteral("b"));
    addChild(b, QStringLiteral("sub"));
    addFile(a, QStringLiteral("x.txt"), 10);
    addFile(a, QStringLiteral("y.txt"), 20);
    addFile(b, QStringLiteral("y.txt"), 5);
    const std::vector<const FsNode *> roots{root.get()};

    // Legality on the base tree.
    check(checkFileOp(fileOp(OpKind::RenameFile, "/r/a", "x.txt", "z.txt"), a, nullptr) ==
              MoveLegality::Ok,
          "rename to a free name is Ok");
    check(checkFileOp(fileOp(OpKind::RenameFile, "/r/a", "x.txt", "y.txt"), a, nullptr) ==
              MoveLegality::Collision,
          "rename onto an existing file collides");
    check(checkFileOp(fileOp(OpKind::RenameFile, "/r/a", "x.txt", "a/b"), a, nullptr) ==
              MoveLegality::BadName,
          "rename with a slash is BadName");
    check(checkFileOp(fileOp(OpKind::RenameFile, "/r/a", "x.txt", "x.txt"), a, nullptr) ==
              MoveLegality::SameNode,
          "rename to itself is a no-op");
    check(checkFileOp(fileOp(OpKind::RenameFile, "/r/a", "nope", "q"), a, nullptr) ==
              MoveLegality::SameNode,
          "rename of a missing entry is the null verdict");
    check(checkFileOp(fileOp(OpKind::MoveFile, "/r/a", "y.txt", "/r/b"), a, b) ==
              MoveLegality::Collision,
          "move onto a same-named file collides");
    check(checkFileOp(fileOp(OpKind::MoveFile, "/r/b", "y.txt", "/r/b"), b, b) ==
              MoveLegality::SameNode,
          "move into its own directory is a no-op");
    check(checkFileOp(fileOp(OpKind::RenameFile, "/r/b", "y.txt", "sub"), b, nullptr) ==
              MoveLegality::Collision,
          "rename onto a subdirectory name collides");
    check(checkFileOp(fileOp(OpKind::TrashFile, "/r/a", "x.txt"), a, nullptr) == MoveLegality::Ok,
          "trash of an existing entry is Ok");

    // Replay: rename, then move the renamed file (chained by name), then trash another.
    std::vector<MoveOp> ops{fileOp(OpKind::RenameFile, "/r/a", "x.txt", "z.txt"),
                            fileOp(OpKind::MoveFile, "/r/a", "z.txt", "/r/b"),
                            fileOp(OpKind::TrashFile, "/r/b", "y.txt")};
    auto forest = projectForest(roots, ops);
    const FsNode *pa = child(forest[0].get(), QStringLiteral("a"));
    const FsNode *pb = child(forest[0].get(), QStringLiteral("b"));
    check(pa && pb, "projection keeps both directories");
    if (pa && pb) {
        check(!hasFile(pa, QStringLiteral("x.txt")) && !hasFile(pa, QStringLiteral("z.txt")),
              "renamed file left a");
        check(hasFile(pb, QStringLiteral("z.txt")), "renamed file arrived in b");
        check(!hasFile(pb, QStringLiteral("y.txt")), "trashed file is gone from b");
        check(pa->fileCount == 1 && pa->sizeBytes == 20, "a's counts follow the move");
        check(pb->fileCount == 1 && pb->sizeBytes == 10, "b's counts follow move + trash");
    }
    check(a->files.size() == 2 && b->files.size() == 1, "the scanned tree is untouched");

    const std::vector<MoveLegality> verdicts = replayLegality(roots, ops);
    check(verdicts.size() == 3 && verdicts[0] == MoveLegality::Ok &&
              verdicts[1] == MoveLegality::Ok && verdicts[2] == MoveLegality::Ok,
          "chained file ops judged Ok in apply order");
    // A move that collides only because of an earlier op is judged against the evolving tree.
    ops.push_back(fileOp(OpKind::MoveFile, "/r/a", "y.txt", "/r/b")); // b lost y.txt: now free
    check(replayLegality(roots, ops)[3] == MoveLegality::Ok,
          "a move freed by an earlier trash is Ok in order");
}

int main() {
    testLedger();
    testFileOps();
    testIdentity();
    testMove();
    testCycleAndCollision();
    testRootAndUnresolved();
    testCrossRoot();
    testDuplicateOpIdempotent();
    testCheckMove();
    testChainedMoveIdentity();

    if (g_failures == 0) {
        std::puts("all move-model tests passed");
        return 0;
    }
    std::fprintf(stderr, "%d move-model test(s) failed\n", g_failures);
    return 1;
}
