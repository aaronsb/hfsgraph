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

// ---- Legality -----------------------------------------------------------------

namespace {

// True if `node` is `ancestor` or sits underneath it — i.e. moving `ancestor` under
// `node` would form a cycle.
bool isSelfOrDescendant(const FsNode *node, const FsNode *ancestor) {
    for (const FsNode *p = node; p; p = p->parent)
        if (p == ancestor)
            return true;
    return false;
}

} // namespace

MoveLegality checkMove(const FsNode *src, const FsNode *dst) {
    if (!src || !dst || src == dst)
        return MoveLegality::SameNode;
    if (!src->parent) // a root surface has no parent to detach from
        return MoveLegality::SourceIsRoot;
    if (isSelfOrDescendant(dst, src)) // dest is the source or under it
        return MoveLegality::Cycle;
    for (const auto &c : dst->children) // a child of that name already lives at dest
        if (c->name == src->name)
            return MoveLegality::Collision; // also catches a no-op drop onto src's own parent
    return MoveLegality::Ok;
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
    n->originalPath = src->originalPath; // the scanned location; a move rewrites path, not this
    n->fp = src->fp;                     // carry the runtime fingerprint into the projection
    n->parent = parent;
    const MemberKey key = keyFor(*src);
    n->identity = key; // pin the original key so later moves (which recompute path) still resolve
    byKey.insert(key, n.get());
    for (const auto &c : src->children)
        n->children.push_back(deepCopy(c.get(), n.get(), byKey));
    return n;
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

// Detach `src` from its parent (taking ownership) and re-home it under `dst`, then refresh
// its subtree paths. Caller must have already checked legality. Returns false only if `src`
// wasn't found among its parent's children (shouldn't happen — stay safe).
bool applyMoveTo(FsNode *src, FsNode *dst) {
    auto &siblings = src->parent->children;
    std::unique_ptr<FsNode> owned;
    for (auto it = siblings.begin(); it != siblings.end(); ++it)
        if (it->get() == src) {
            owned = std::move(*it);
            siblings.erase(it);
            break;
        }
    if (!owned)
        return false;
    owned->parent = dst;
    dst->children.push_back(std::move(owned));
    recomputePaths(src);
    return true;
}

} // namespace

std::vector<std::unique_ptr<FsNode>> projectForest(const std::vector<const FsNode *> &roots,
                                                   const std::vector<MoveOp> &ops) {
    // NOTE: keys are paths today, so two roots sharing a path (the same folder added
    // as two bases) alias in byKey and a move can retarget the wrong one. Harmless
    // (no crash, just surprising) and resolved by ADR-100 durable ids (task #14).
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
        if (checkMove(src, dst) != MoveLegality::Ok)
            continue; // unresolved-to-illegal ops are skipped — replay never corrupts
        applyMoveTo(src, dst);
    }
    return out;
}

std::vector<MoveLegality> replayLegality(const std::vector<const FsNode *> &roots,
                                         const std::vector<MoveOp> &ops) {
    std::vector<std::unique_ptr<FsNode>> work;
    QHash<MemberKey, FsNode *> byKey;
    work.reserve(roots.size());
    for (const FsNode *r : roots)
        work.push_back(r ? deepCopy(r, nullptr, byKey) : nullptr);

    std::vector<MoveLegality> out;
    out.reserve(ops.size());
    for (const MoveOp &op : ops) {
        const auto sit = byKey.constFind(op.source);
        const auto dit = byKey.constFind(op.destParent);
        if (sit == byKey.constEnd() || dit == byKey.constEnd()) {
            out.push_back(MoveLegality::SameNode); // unresolved → the null-node verdict
            continue;
        }
        FsNode *src = sit.value();
        FsNode *dst = dit.value();
        const MoveLegality legal = checkMove(src, dst);
        out.push_back(legal);
        if (legal == MoveLegality::Ok)
            applyMoveTo(src, dst); // apply so later ops see the evolving tree
    }
    return out;
}

} // namespace core
