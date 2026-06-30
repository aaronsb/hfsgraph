// Pure model of a scanned directory tree. No Qt-GUI / no rendering concerns —
// this is the "core" layer the UI merely renders (ADR-101, ADR-200). Keeping it
// dependency-light is deliberate: it is the seam a future Rust core would sit
// behind (CONCEPT.md "Deferred research").
#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <vector>

namespace core {

// The *ephemeral* runtime identity of a node on disk (ADR-100): the (device, inode)
// pair that `mv` preserves within one filesystem, plus mtime/size. Recorded at scan
// time and re-checked at commit time to confirm "we are moving what we scanned" — it
// is a fingerprint, *never* the durable key (inodes recycle, are per-filesystem, and
// don't survive backup/restore). `valid` is false when the stat failed.
struct Fingerprint {
    quint64 dev = 0;       // st_dev (filesystem / btrfs subvolume id)
    quint64 ino = 0;       // st_ino (recycled after delete — not durable)
    qint64 mtime = 0;      // st_mtime, Unix epoch seconds
    qint64 size = 0;       // st_size
    bool valid = false;    // false if the node couldn't be stat'd
};

// One non-directory entry directly inside a node (a regular file, or a symlink —
// including a symlinked directory, which the scanner records here rather than
// descending). Carries the metadata the `ls -l`/Details rung renders; `name` is the
// stable identity used by groups (ADR-102) and move keys (ADR-302).
struct FileEntry {
    QString name;            // basename (display + identity)
    qint64 sizeBytes = 0;    // size on disk
    qint64 mtime = 0;        // last-modified, Unix epoch seconds (0 = unknown)
    uint perms = 0;          // QFileDevice::Permissions bits (decoded by the UI)
    bool isSymlink = false;  // a symbolic link (rendered with a leading 'l')
    QString linkTarget;      // symlink destination, empty for a plain file
};

// One directory in the scanned tree. `children` are subdirectories; `files` are
// the non-directory entries directly inside this directory (a node's "file listing").
struct FsNode {
    QString path;                                  // absolute path
    QString name;                                  // basename (display)
    std::vector<std::unique_ptr<FsNode>> children; // subdirectories
    std::vector<FileEntry> files;                  // non-directory entries, this dir
    int fileCount = 0;                             // total regular files
    qint64 sizeBytes = 0;                          // sum of this dir's regular-file sizes
    bool truncatedDepth = false;                   // children exist on disk but scan stopped
    FsNode *parent = nullptr;
    // The durable directory id (ADR-100): a UUID stored in the `user.hfsgraph.id` xattr
    // that travels with the directory through `mv`/rename. The scanner reads it (empty
    // when a dir hasn't been stamped yet — the common case, since stamping is lazy/on-
    // touch); the commit engine writes it. keyFor() is the single seam: it prefers this
    // id and falls back to `path` while a node is unstamped. The projection deep-copy
    // pins each copy's key here so an op/group survives a later move that recomputes path.
    QString identity;
    // Where this node was *scanned* from — its on-disk path, stable even after the
    // projection rewrites `path` to a staged move's destination. The diff overlay reads
    // it to tell "this node actually relocated" (path != originalPath) apart from a node
    // an op merely *named* but replay left in place (ADR-302 #12); this is independent of
    // the identity scheme, so it stays correct once `identity` is a UUID, not a path.
    QString originalPath;
    Fingerprint fp;                                // ephemeral (dev, inode) runtime check

    bool isLeaf() const { return children.empty(); }
};

} // namespace core
