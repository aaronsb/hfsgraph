// Read-only filesystem scanner: builds an FsNode tree from a directory.
// Strictly read-only (the POC never writes). Lives in the core layer.
#pragma once

#include "fsnode.h"
#include <memory>

namespace core {

class Scanner {
  public:
    // Scan `rootPath` to `maxDepth` (root is depth 0; maxDepth < 0 means unlimited).
    // Symlinked directories are recorded but never descended (avoids cycles).
    // Returns nullptr if the root is not an accessible directory.
    static std::unique_ptr<FsNode> scan(const QString &rootPath, int maxDepth);
};

} // namespace core
