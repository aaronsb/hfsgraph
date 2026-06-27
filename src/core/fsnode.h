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

// One directory in the scanned tree. `children` are subdirectories; `files` are
// the regular-file names directly inside this directory (a node's "file listing").
struct FsNode {
    QString path;                                  // absolute path
    QString name;                                  // basename (display)
    std::vector<std::unique_ptr<FsNode>> children; // subdirectories
    QStringList files;                             // regular-file names, this dir
    int fileCount = 0;                             // total regular files
    qint64 sizeBytes = 0;                          // sum of this dir's regular-file sizes
    bool truncatedDepth = false;                   // children exist on disk but scan stopped
    FsNode *parent = nullptr;

    bool isLeaf() const { return children.empty(); }
};

} // namespace core
