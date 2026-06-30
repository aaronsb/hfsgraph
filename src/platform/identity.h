// OS-facility layer for ADR-100 durable identity. This is the *only* place that talks
// to xattrs and stat(2); the core model stays dependency-light (the "Rust core" seam,
// CONCEPT.md) and merely holds the results (core::FsNode::identity / ::fp). Linux-only
// for now — the xattr name and syscalls are platform-specific (ADR-400).
#pragma once

#include "core/fsnode.h"

#include <QString>

namespace platform {

// The xattr the durable UUID lives in. `user.*` travels with the directory through
// `mv` and xattr-aware copies (cp -a, rsync -X, btrfs send/receive); see ADR-100.
inline constexpr const char *kDurableIdAttr = "user.hfsgraph.id";

// Read the durable id stamped on `path`, or an empty string when the directory has
// never been stamped, the attribute is missing, or the filesystem doesn't support
// `user.*` xattrs. Pure read — never writes (the scan must stay read-only, ADR-100).
QString readDurableId(const QString &path);

// Stamp `path` with durable id `id` (a no-op-overwriting set). Returns false when the
// filesystem rejects user xattrs (ENOTSUP) or we lack permission — the caller then
// keeps falling back to path keys rather than failing. This is the lazy "on touch"
// write; the scan never calls it.
bool stampDurableId(const QString &path, const QString &id);

// True if `path`'s filesystem accepts `user.*` xattrs (probes by reading our attr;
// distinguishes "unsupported" from "supported but unstamped"). Used by tests and by
// callers that want to warn before relying on durability.
bool xattrSupported(const QString &path);

// A freshly minted durable id (a UUID, no surrounding braces). Pure — no disk I/O.
QString newDurableId();

// The ephemeral (dev, inode, mtime, size) fingerprint of `path` via lstat(2) (the link
// itself, not its target). `valid` is false if the stat fails. ADR-100 commit-time check.
// NOTE: readDurableId/stampDurableId use getxattr/setxattr (which *follow* symlinks), so
// for consistency every function here expects a real directory path — the scanner only
// ever descends real dirs (scanner.cpp), so id and fingerprint always describe the same
// object. Don't hand these a symlink path.
core::Fingerprint statFingerprint(const QString &path);

// Lazy stamping (ADR-100): ensure the node's *on-disk* directory carries a durable id,
// and record it on `node.identity`. Reads the dir's xattr (the authority — a non-empty
// in-memory `identity` may just be the projection's path-fallback key, not a real id):
// adopts an existing id, otherwise mints one and writes it. Stamps `originalPath`, since
// a projection rewrites `path` to a staged destination that doesn't exist on disk.
// Returns the durable id, or empty when the write failed (e.g. unsupported fs, so the
// node stays path-keyed). Idempotent. (Orphan re-adoption of a stripped id is #15.)
QString ensureDurableId(core::FsNode &node);

} // namespace platform
