// Pure model of a scanned directory tree. No Qt-GUI / no rendering concerns —
// this is the "core" layer the UI merely renders (ADR-101, ADR-200). Keeping it
// dependency-light is deliberate: it is the seam a future Rust core would sit
// behind (CONCEPT.md "Deferred research").
#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace core {

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

    bool isLeaf() const { return children.empty(); }
};

} // namespace core
