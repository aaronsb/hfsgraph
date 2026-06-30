// Unit tests for durable directory identity (ADR-100 / task #14): the platform xattr +
// fingerprint primitives and how keyFor() consumes them. Touches a real temp directory
// (xattrs and stat have no in-memory fake worth building); the xattr round-trip asserts
// self-skip when the temp filesystem doesn't support user.* attributes, so the test is
// green on a stripped-down CI fs while still being meaningful on a normal one. Plain
// assert harness registered with ctest, matching tests/move_test.cpp.

#include "core/fsnode.h"
#include "core/group.h"
#include "platform/identity.h"

#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void testFingerprint(const QString &dir) {
    const core::Fingerprint fp = platform::statFingerprint(dir);
    check(fp.valid, "fingerprint: an existing dir stats");
    check(fp.dev != 0 && fp.ino != 0, "fingerprint: dev and inode are populated");

    const core::Fingerprint gone = platform::statFingerprint(dir + QStringLiteral("/nope"));
    check(!gone.valid, "fingerprint: a missing path is invalid");
}

void testNewId() {
    const QString a = platform::newDurableId();
    const QString b = platform::newDurableId();
    check(!a.isEmpty(), "newDurableId: non-empty");
    check(a != b, "newDurableId: two ids differ");
}

void testKeyFor() {
    // keyFor() is the seam: a UUID identity wins, an empty one falls back to path.
    core::FsNode n;
    n.path = QStringLiteral("/r/a");
    check(core::keyFor(n) == QStringLiteral("/r/a"), "keyFor: unstamped node keys by path");
    n.identity = QStringLiteral("uuid-1234");
    check(core::keyFor(n) == QStringLiteral("uuid-1234"), "keyFor: stamped node keys by id");

    // keyForFile tracks keyFor(dir), so a file follows its directory's durable key.
    check(core::keyForFile(n, QStringLiteral("f.txt")) == QStringLiteral("uuid-1234/f.txt"),
          "keyForFile: keyed off the durable dir id");
    n.identity.clear();
    check(core::keyForFile(n, QStringLiteral("f.txt")) == QStringLiteral("/r/a/f.txt"),
          "keyForFile: falls back to dir path when unstamped");
}

// The xattr write path — only meaningful where the filesystem supports user.* attrs.
void testStampRoundTrip(const QString &dir) {
    if (!platform::xattrSupported(dir)) {
        std::fprintf(stderr, "SKIP: %s lacks user.* xattr support\n", qPrintable(dir));
        return;
    }
    check(platform::readDurableId(dir).isEmpty(), "stamp: a fresh dir is unstamped");

    const QString id = platform::newDurableId();
    check(platform::stampDurableId(dir, id), "stamp: write succeeds on a supported fs");
    check(platform::readDurableId(dir) == id, "stamp: read returns what was written");

    // A value longer than readDurableId's 64-byte stack buffer drives its ERANGE
    // size-query → heap-fetch path (we only ever store UUIDs, but the branch must work).
    QTemporaryDir big;
    check(big.isValid(), "stamp: temp dir for the long-value case");
    const QString longVal = QString(200, QLatin1Char('x'));
    check(platform::stampDurableId(big.path(), longVal), "stamp: long value writes");
    check(platform::readDurableId(big.path()) == longVal, "stamp: long value round-trips (ERANGE path)");

    // ensureDurableId on an already-stamped node (the scanner read the id) is a no-op.
    core::FsNode node;
    node.path = dir;
    node.identity = id;
    check(platform::ensureDurableId(node) == id, "ensure: keeps an existing id");

    // ensureDurableId on an unstamped node mints + persists + adopts; idempotent. It must
    // stamp the on-disk location (originalPath), not `path` — model a projection node whose
    // `path` points at a staged, not-yet-existent destination.
    QTemporaryDir fresh;
    check(fresh.isValid(), "ensure: temp dir created");
    core::FsNode n2;
    n2.originalPath = fresh.path();
    n2.path = fresh.path() + QStringLiteral("/../staged-destination-that-does-not-exist");
    const QString minted = platform::ensureDurableId(n2);
    check(!minted.isEmpty() && n2.identity == minted, "ensure: mints and adopts a new id");
    check(platform::readDurableId(fresh.path()) == minted,
          "ensure: stamps the on-disk originalPath, not the staged path");
    check(platform::ensureDurableId(n2) == minted, "ensure: second call is idempotent");

    // The whole point: the id travels with the directory through a rename, and the inode
    // (the ephemeral fingerprint) is preserved by an in-filesystem mv.
    const core::Fingerprint before = platform::statFingerprint(dir);
    const QString moved = dir + QStringLiteral("-moved");
    check(QDir().rename(dir, moved), "rename: dir renamed");
    check(platform::readDurableId(moved) == id, "rename: durable id survives the move");
    const core::Fingerprint after = platform::statFingerprint(moved);
    check(after.valid && after.ino == before.ino, "rename: inode preserved within the filesystem");
    QDir().rename(moved, dir); // restore so QTemporaryDir cleans up
}

} // namespace

int main() {
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temp dir\n");
        return 1;
    }
    // A subdirectory we own and can rename (renaming the QTemporaryDir root itself would
    // confuse its auto-cleanup).
    const QString dir = tmp.path() + QStringLiteral("/work");
    if (!QDir().mkpath(dir)) {
        std::fprintf(stderr, "FAIL: could not create the work dir\n");
        return 1;
    }

    testFingerprint(dir);
    testNewId();
    testKeyFor();
    testStampRoundTrip(dir);

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all identity checks passed\n");
    return 0;
}
