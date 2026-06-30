// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Commit engine — dry-run verification half (ADR-200 / task #16a). ADR-200 is a staged
// transaction: edits accumulate in the Ledger (ADR-302), then before anything touches disk
// the plan is *verified* — applied to a virtual view of the tree and reported per-op as
// legal/illegal with a reason. This module is that verification. It is pure (reads disk
// state through an injected stat function, writes nothing) and UI-free, so it is exhaustively
// testable — the isolation ADR-200/ADR-400 lean on instead of language safety guarantees.
//
// The actual apply (btrfs snapshot, topological mv with staging names, rollback, durable-id
// stamping, sidecar update) is the separate, more dangerous half (#16b) and is NOT here.
#pragma once

#include "core/fsnode.h" // Fingerprint
#include "core/move.h"   // MoveOp, MoveLegality

#include <QString>

#include <functional>
#include <vector>

namespace core {

// Per-op verdict. Ok means the op is structurally legal and its source on disk still matches
// what we scanned; the rest are blocking reasons the report explains. (Permissions,
// immutability, git-boundary, and symlink classification from ADR-200 are deferred with the
// apply half — this slice covers structure, identity/drift, and volume boundaries.)
enum class VerifyStatus {
    Ok,
    Unresolved,    // source or destination key doesn't resolve in the scanned forest
    IllegalMove,   // structurally illegal (collision / cycle / root / same) — see `legality`
    SourceMissing, // the source directory no longer exists on disk
    SourceDrifted, // the source on disk is a different object (dev/inode) than we scanned
    DestMissing,   // the destination parent no longer exists on disk
    CrossVolume,   // source and destination parent are on different devices (EXDEV: copy+delete)
};

QString verifyStatusLabel(VerifyStatus s); // short human label for a report row

// One op's verification: the op, its verdict, the structural detail when IllegalMove, and a
// one-line human-readable explanation for the report.
struct OpVerification {
    MoveOp op;
    VerifyStatus status = VerifyStatus::Ok;
    MoveLegality legality = MoveLegality::Ok;
    QString detail;
};

// The verified plan: one entry per active ledger op, index-aligned.
struct CommitPlan {
    std::vector<OpVerification> ops;
    bool allClear() const; // every op Ok (safe to apply)
    int okCount() const;
    int blockedCount() const;
};

// How verifyPlan reads current on-disk state for a path; injectable so the engine can be
// tested without touching the filesystem. The default overload uses platform::statFingerprint.
using FingerprintFn = std::function<Fingerprint(const QString &)>;

// Verify the active plan against the *current* disk. For each op: resolve its source and
// destination-parent keys in the scanned forest; check structural legality (checkMove);
// confirm the source still exists and is the same object we scanned (its recorded fingerprint
// vs. a fresh stat of its on-disk `originalPath`); and flag a cross-device move. `roots` are
// the immutable scanned sources. Reads disk only through `statOf`, writes nothing.
CommitPlan verifyPlan(const std::vector<const FsNode *> &roots, const std::vector<MoveOp> &ops,
                      const FingerprintFn &statOf);

// Convenience overload using the real platform stat (platform::statFingerprint).
CommitPlan verifyPlan(const std::vector<const FsNode *> &roots, const std::vector<MoveOp> &ops);

} // namespace core
