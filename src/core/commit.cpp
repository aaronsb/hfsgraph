// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/commit.h"

#include "core/group.h" // keyFor
#include "platform/identity.h"

#include <QHash>

namespace core {

QString verifyStatusLabel(VerifyStatus s) {
    switch (s) {
    case VerifyStatus::Ok:
        return QStringLiteral("OK");
    case VerifyStatus::Unresolved:
        return QStringLiteral("unresolved");
    case VerifyStatus::IllegalMove:
        return QStringLiteral("illegal");
    case VerifyStatus::SourceMissing:
        return QStringLiteral("source missing");
    case VerifyStatus::SourceDrifted:
        return QStringLiteral("source changed on disk");
    case VerifyStatus::DestMissing:
        return QStringLiteral("destination missing");
    case VerifyStatus::CrossVolume:
        return QStringLiteral("cross-volume");
    }
    return QStringLiteral("unknown");
}

bool CommitPlan::allClear() const {
    for (const auto &o : ops)
        if (o.status != VerifyStatus::Ok)
            return false;
    return !ops.empty();
}

int CommitPlan::okCount() const {
    int n = 0;
    for (const auto &o : ops)
        if (o.status == VerifyStatus::Ok)
            ++n;
    return n;
}

int CommitPlan::blockedCount() const {
    return static_cast<int>(ops.size()) - okCount();
}

namespace {

void indexByKey(const FsNode *n, QHash<MemberKey, const FsNode *> &out) {
    if (!n)
        return;
    out.insert(keyFor(*n), n); // path/identity aliasing: last wins (ADR-100)
    for (const auto &c : n->children)
        indexByKey(c.get(), out);
}

// The on-disk path to stat for a node: its scanned location, stable even if the projection
// would rewrite `path` to a staged destination (ADR-100 originalPath).
QString diskPath(const FsNode *n) {
    return n->originalPath.isEmpty() ? n->path : n->originalPath;
}

QString moveLegalityLabel(MoveLegality l) {
    switch (l) {
    case MoveLegality::Ok:
        return QStringLiteral("ok");
    case MoveLegality::SameNode:
        return QStringLiteral("same node / no-op");
    case MoveLegality::SourceIsRoot:
        return QStringLiteral("source is a root surface");
    case MoveLegality::Cycle:
        return QStringLiteral("would form a cycle");
    case MoveLegality::Collision:
        return QStringLiteral("name already exists at destination");
    case MoveLegality::BadName:
        return QStringLiteral("not a usable file name");
    }
    return QStringLiteral("illegal");
}

// `replayed` carries the entry's on-disk path as replay resolved it at this op (a
// file renamed by an earlier op is still at its scanned path on disk).
OpVerification verifyFileOp(const MoveOp &op, const FsNode *dir, const FsNode *dst,
                            const ReplayVerdict &replayed, const FingerprintFn &statOf) {
    OpVerification v;
    v.op = op;
    v.legality = replayed.legality;
    const QString what = opKindLabel(op.kind) + QLatin1Char(' ') + op.fileName;
    if (replayed.legality != MoveLegality::Ok) {
        v.status = VerifyStatus::IllegalMove;
        v.detail = QStringLiteral("%1: %2").arg(what, moveLegalityLabel(replayed.legality));
        return v;
    }
    const QString filePath = replayed.subjectOrigin.isEmpty()
                                 ? diskPath(dir) + QLatin1Char('/') + op.fileName
                                 : replayed.subjectOrigin;
    const Fingerprint now = statOf(filePath);
    if (!now.valid) {
        v.status = VerifyStatus::SourceMissing;
        v.detail = QStringLiteral("%1: no longer exists at %2").arg(what, filePath);
        return v;
    }
    // The holding directory must still be the object we scanned; one that can't be
    // stat'd at all counts as missing, not as unchanged.
    const Fingerprint dirNow = statOf(diskPath(dir));
    if (!dirNow.valid) {
        v.status = VerifyStatus::SourceMissing;
        v.detail =
            QStringLiteral("%1: its directory no longer exists at %2").arg(what, diskPath(dir));
        return v;
    }
    if (dir->fp.valid && (dirNow.dev != dir->fp.dev || dirNow.ino != dir->fp.ino)) {
        v.status = VerifyStatus::SourceDrifted;
        v.detail = QStringLiteral("%1: its directory is a different object now (re-scan needed)")
                       .arg(what);
        return v;
    }
    if (op.kind == OpKind::MoveFile) {
        const Fingerprint dstFp = statOf(diskPath(dst));
        if (!dstFp.valid) {
            v.status = VerifyStatus::DestMissing;
            v.detail = QStringLiteral("%1 → %2: destination no longer exists").arg(what, dst->name);
            return v;
        }
        if (now.dev != dstFp.dev) {
            v.status = VerifyStatus::CrossVolume;
            v.detail = QStringLiteral("%1 → %2: crosses a volume boundary (copy+delete, not mv)")
                           .arg(what, dst->name);
            return v;
        }
    }
    v.status = VerifyStatus::Ok;
    v.detail = op.kind == OpKind::MoveFile     ? QStringLiteral("%1 → %2: OK").arg(what, dst->name)
               : op.kind == OpKind::RenameFile ? QStringLiteral("%1 → %2: OK").arg(what, op.newName)
                                               : QStringLiteral("%1: OK").arg(what);
    return v;
}

OpVerification verifyOne(const MoveOp &op, const FsNode *src, const FsNode *dst,
                         const ReplayVerdict &verdict, const FingerprintFn &statOf) {
    if (op.isFileOp())
        return verifyFileOp(op, src, dst, verdict, statOf);
    const MoveLegality replayed = verdict.legality;
    OpVerification v;
    v.op = op;

    v.legality = replayed;
    if (replayed != MoveLegality::Ok) {
        v.status = VerifyStatus::IllegalMove;
        v.detail = QStringLiteral("%1 → %2: %3")
                       .arg(op.sourceName, dst->name, moveLegalityLabel(replayed));
        return v;
    }

    // Identity / drift: is the source still the object we scanned? A missing path or a
    // changed (device, inode) means we must not move it — we'd be moving the wrong thing.
    const Fingerprint now = statOf(diskPath(src));
    if (!now.valid) {
        v.status = VerifyStatus::SourceMissing;
        v.detail = QStringLiteral("%1: no longer exists at %2").arg(op.sourceName, diskPath(src));
        return v;
    }
    if (src->fp.valid && (now.dev != src->fp.dev || now.ino != src->fp.ino)) {
        v.status = VerifyStatus::SourceDrifted;
        v.detail = QStringLiteral("%1: a different object now occupies %2 (re-scan needed)")
                       .arg(op.sourceName, diskPath(src));
        return v;
    }

    // The destination parent must still exist to receive the move (apply would fail otherwise).
    const Fingerprint dstFp = statOf(diskPath(dst));
    if (!dstFp.valid) {
        v.status = VerifyStatus::DestMissing;
        v.detail = QStringLiteral("%1 → %2: destination no longer exists at %3")
                       .arg(op.sourceName, dst->name, diskPath(dst));
        return v;
    }

    // Volume boundary: a rename across devices fails with EXDEV (needs copy+delete, which the
    // apply half will classify and cost; flagged here so the report is honest).
    if (now.dev != dstFp.dev) {
        v.status = VerifyStatus::CrossVolume;
        v.detail = QStringLiteral("%1 → %2: crosses a volume boundary (copy+delete, not mv)")
                       .arg(op.sourceName, dst->name);
        return v;
    }

    v.status = VerifyStatus::Ok;
    v.detail = QStringLiteral("%1 → %2: OK").arg(op.sourceName, dst->name);
    return v;
}

} // namespace

CommitPlan verifyPlan(const std::vector<const FsNode *> &roots, const std::vector<MoveOp> &ops,
                      const FingerprintFn &statOf) {
    QHash<MemberKey, const FsNode *> byKey;
    for (const FsNode *r : roots)
        indexByKey(r, byKey);
    // Structural legality from an ordered replay, so a chained op (op B relies on what op A
    // cleared) is judged in apply order, not against the static base (#16a review).
    const std::vector<ReplayVerdict> replayed = replayVerdicts(roots, ops);

    CommitPlan plan;
    plan.ops.reserve(ops.size());
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const MoveOp &op = ops[i];
        const FsNode *src = byKey.value(op.source, nullptr);
        const FsNode *dst = byKey.value(op.destParent, nullptr);
        const bool needsDst = !op.isFileOp() || op.kind == OpKind::MoveFile;
        if (!src || (needsDst && !dst)) {
            OpVerification v;
            v.op = op;
            v.status = VerifyStatus::Unresolved;
            v.detail = op.isFileOp() && op.kind != OpKind::MoveFile
                           ? QStringLiteral("%1 %2: its directory no longer resolves in the scan")
                                 .arg(opKindLabel(op.kind), op.fileName)
                           : QStringLiteral("%1 → %2: a node no longer resolves in the scan")
                                 .arg(op.sourceName, op.destParent);
            plan.ops.push_back(v);
            continue;
        }
        plan.ops.push_back(verifyOne(op, src, dst, replayed[i], statOf));
    }
    return plan;
}

CommitPlan verifyPlan(const std::vector<const FsNode *> &roots, const std::vector<MoveOp> &ops) {
    return verifyPlan(roots, ops, [](const QString &p) { return platform::statFingerprint(p); });
}

} // namespace core
