// Unit tests for the move-staging model (ADR-302 / task #9): core::Ledger editing
// semantics and core::projectForest replay. Pure core, no Qt-GUI — a plain assert
// harness registered with ctest (no extra test-framework dependency).

#include "core/fsnode.h"
#include "core/move.h"

#include <QString>

#include <cstdio>
#include <vector>

using core::FsNode;
using core::Ledger;
using core::MoveOp;

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
    c->parent = parent;
    c->fileCount = ownFiles;
    FsNode *raw = c.get();
    parent->children.push_back(std::move(c));
    return raw;
}

std::unique_ptr<FsNode> makeRoot(const QString &path) {
    auto r = std::make_unique<FsNode>();
    r->path = path;
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
    const FsNode *movedLeaf = child(child(proj1[0].get(), QStringLiteral("b")), QStringLiteral("leaf"));
    check(movedLeaf && movedLeaf->path == QStringLiteral("/r/b/leaf"), "chained: first move landed");
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

int main() {
    testLedger();
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
