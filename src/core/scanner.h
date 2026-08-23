// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Read-only filesystem scanner: builds an FsNode tree from a directory.
// Strictly read-only (the POC never writes). Lives in the core layer.
#pragma once

#include "fsnode.h"
#include <memory>
#include <vector>

namespace core {

class Scanner {
  public:
    // Scan `rootPath` to `maxDepth` (root is depth 0; maxDepth < 0 means unlimited).
    // Symlinked directories are recorded but never descended (avoids cycles).
    // Returns nullptr if the root is not an accessible directory.
    static std::unique_ptr<FsNode> scan(const QString &rootPath, int maxDepth);

    // Scan the subdirectories of an already-scanned directory, `levels` deep below it
    // (1 = its immediate subdirectories), returning them detached — `parent` pointers
    // set to `node`, nothing attached. Safe to run on a worker thread while the tree
    // is displayed; the caller grafts the result on the GUI thread (the scene's
    // lazy-deepening path). Reads only `node.path`.
    static std::vector<std::unique_ptr<FsNode>> scanChildren(const FsNode &node, int levels);
};

} // namespace core
