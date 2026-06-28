#include "scanner.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace core {

namespace {

std::unique_ptr<FsNode> scanDir(const QFileInfo &dirInfo, int depth, int maxDepth, FsNode *parent) {
    auto node = std::make_unique<FsNode>();
    node->path = dirInfo.absoluteFilePath();
    node->name = dirInfo.fileName().isEmpty() ? node->path : dirInfo.fileName();
    node->parent = parent;

    QDir dir(node->path);
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                           QDir::DirsFirst | QDir::Name);

    const bool canDescend = (maxDepth < 0) || (depth < maxDepth);
    bool sawSubdir = false;

    for (const QFileInfo &entry : entries) {
        // Treat symlinked dirs as leaves (record, never descend — avoids cycles).
        if (entry.isDir() && !entry.isSymLink()) {
            sawSubdir = true;
            if (canDescend) {
                node->children.push_back(scanDir(entry, depth + 1, maxDepth, node.get()));
            }
        } else {
            node->fileCount++;
            node->sizeBytes += entry.size();
            FileEntry fe;
            fe.name = entry.fileName();
            // QFileInfo follows symlinks, so size/mtime/perms here are the *target's*
            // (effective values), not the link's own — the Details rung still flags a
            // link with a leading 'l'. True lstat parity can come later if needed.
            fe.sizeBytes = entry.size();
            fe.mtime = entry.lastModified().toSecsSinceEpoch();
            fe.perms = static_cast<uint>(entry.permissions());
            fe.isSymlink = entry.isSymLink();
            if (fe.isSymlink)
                fe.linkTarget = entry.symLinkTarget();
            node->files.push_back(std::move(fe));
        }
    }

    // Children exist on disk but we stopped early — mark so the UI can show it.
    node->truncatedDepth = sawSubdir && !canDescend;
    return node;
}

} // namespace

std::unique_ptr<FsNode> Scanner::scan(const QString &rootPath, int maxDepth) {
    QFileInfo info(rootPath);
    if (!info.exists() || !info.isDir())
        return nullptr;
    return scanDir(info, 0, maxDepth, nullptr);
}

} // namespace core
