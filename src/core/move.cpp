#include "core/move.h"

#include "core/fsnode.h"

#include <QHash>

#include <algorithm>

namespace core {

// ---- Ledger -------------------------------------------------------------------

void Ledger::append(const MoveOp &op) {
    m_redo.clear(); // a fresh edit invalidates the redo history
    m_ops.push_back(op);
    m_step = size(); // preview the whole plan after an append
}

bool Ledger::undo() {
    if (m_ops.empty())
        return false;
    m_redo.push_back(m_ops.back());
    m_ops.pop_back();
    if (m_step > size())
        m_step = size(); // keep the scrub pointer in range
    return true;
}

bool Ledger::redo() {
    if (m_redo.empty())
        return false;
    m_ops.push_back(m_redo.back());
    m_redo.pop_back();
    m_step = size(); // preview through the restored op
    return true;
}

void Ledger::clear() {
    m_ops.clear();
    m_redo.clear();
    m_step = 0;
}

void Ledger::setStep(int k) {
    m_step = std::clamp(k, 0, size());
}

std::vector<MoveOp> Ledger::active() const {
    return std::vector<MoveOp>(m_ops.begin(), m_ops.begin() + m_step);
}

// ---- Projection ---------------------------------------------------------------

namespace {

// Deep-copy a node (and its subtree), recording every copy under its *original*
// key, so ops — captured against the pristine tree — resolve by identity even after
// earlier ops relocate nodes.
std::unique_ptr<FsNode> deepCopy(const FsNode *src, FsNode *parent,
                                 QHash<MemberKey, FsNode *> &byKey) {
    auto n = std::make_unique<FsNode>();
    n->path = src->path;
    n->name = src->name;
    n->files = src->files;
    n->fileCount = src->fileCount;
    n->sizeBytes = src->sizeBytes;
    n->truncatedDepth = src->truncatedDepth;
    n->parent = parent;
    byKey.insert(keyFor(*src), n.get()); // key by the original identity
    for (const auto &c : src->children)
        n->children.push_back(deepCopy(c.get(), n.get(), byKey));
    return n;
}

// True if `node` is `ancestor` or sits underneath it — i.e. moving `ancestor` under
// `node` would form a cycle.
bool isSelfOrDescendant(const FsNode *node, const FsNode *ancestor) {
    for (const FsNode *p = node; p; p = p->parent)
        if (p == ancestor)
            return true;
    return false;
}

// Refresh a moved subtree's paths from its new parent down (the node objects and the
// byKey map are unchanged — only the derived `path` fields move).
void recomputePaths(FsNode *n) {
    if (n->parent) {
        const QString &pp = n->parent->path;
        n->path = pp.endsWith(QLatin1Char('/')) ? pp + n->name : pp + QLatin1Char('/') + n->name;
    }
    for (auto &c : n->children)
        recomputePaths(c.get());
}

} // namespace

std::vector<std::unique_ptr<FsNode>> projectForest(const std::vector<const FsNode *> &roots,
                                                   const std::vector<MoveOp> &ops) {
    std::vector<std::unique_ptr<FsNode>> out;
    QHash<MemberKey, FsNode *> byKey;
    out.reserve(roots.size());
    for (const FsNode *r : roots)
        out.push_back(r ? deepCopy(r, nullptr, byKey) : nullptr);

    for (const MoveOp &op : ops) {
        const auto sit = byKey.constFind(op.source);
        const auto dit = byKey.constFind(op.destParent);
        if (sit == byKey.constEnd() || dit == byKey.constEnd())
            continue; // op doesn't resolve against this forest
        FsNode *src = sit.value();
        FsNode *dst = dit.value();
        if (!src->parent) // can't move a root surface
            continue;
        if (isSelfOrDescendant(dst, src)) // cycle: dest is src or under it
            continue;
        bool collision = false; // a child of that name already lives at dest
        for (const auto &c : dst->children)
            if (c->name == src->name) {
                collision = true;
                break;
            }
        if (collision)
            continue;

        // Detach src from its current parent (taking ownership) and re-home it.
        auto &siblings = src->parent->children;
        std::unique_ptr<FsNode> owned;
        for (auto it = siblings.begin(); it != siblings.end(); ++it)
            if (it->get() == src) {
                owned = std::move(*it);
                siblings.erase(it);
                break;
            }
        if (!owned)
            continue; // shouldn't happen (src->parent listed it), but stay safe
        owned->parent = dst;
        dst->children.push_back(std::move(owned));
        recomputePaths(src);
    }
    return out;
}

} // namespace core
