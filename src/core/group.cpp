#include "core/group.h"

#include "core/fsnode.h"

#include <QHash>

namespace core {

MemberKey keyFor(const FsNode &node) {
    return node.path; // TODO(ADR-100): durable id once the scanner stamps one (task #14)
}

MemberKey keyForFile(const FsNode &dir, const QString &filename) {
    // Path-based interim key; ADR-100 will key this as (dir durable id + filename).
    return dir.path.endsWith(QLatin1Char('/')) ? dir.path + filename
                                               : dir.path + QLatin1Char('/') + filename;
}

Group *GroupStore::create(GroupKind kind, const QString &name, const QColor &color) {
    auto g = std::make_unique<Group>();
    g->id = QStringLiteral("g%1").arg(m_nextId++);
    g->name = name;
    g->color = color;
    g->kind = kind;
    Group *raw = g.get();
    m_groups.push_back(std::move(g));
    return raw;
}

void GroupStore::remove(const QString &id) {
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        if ((*it)->id == id) {
            m_groups.erase(it);
            return;
        }
    }
}

Group *GroupStore::find(const QString &id) {
    for (auto &g : m_groups)
        if (g->id == id)
            return g.get();
    return nullptr;
}

const Group *GroupStore::find(const QString &id) const {
    for (const auto &g : m_groups)
        if (g->id == id)
            return g.get();
    return nullptr;
}

std::vector<const Group *> GroupStore::groupsContaining(const MemberKey &k) const {
    std::vector<const Group *> out;
    for (const auto &g : m_groups)
        if (g->contains(k))
            out.push_back(g.get());
    return out;
}

// ---- Rule engine --------------------------------------------------------------

namespace {

// A directory is a git-worktree anchor if it holds a `.git` entry — either a
// `.git` subdirectory (normal clone) or a `.git` *file* (submodule / linked
// worktree, where `.git` is a gitlink pointing elsewhere).
bool isWorktreeAnchor(const FsNode &node) {
    static const QString kGit = QStringLiteral(".git");
    for (const auto &c : node.children)
        if (c->name == kGit)
            return true;
    for (const QString &f : node.files)
        if (f == kGit)
            return true;
    return false;
}

void findAnchors(const FsNode &node, std::vector<const FsNode *> &out) {
    if (isWorktreeAnchor(node))
        out.push_back(&node);
    for (const auto &c : node.children)
        findAnchors(*c, out);
}

// Anchor + every descendant directory and file, by key (exclusions handled by
// Group::contains, not here, so the resolved set stays complete).
void collectSubtree(const FsNode &node, QSet<MemberKey> &out) {
    out.insert(keyFor(node));
    for (const QString &f : node.files)
        out.insert(keyForFile(node, f));
    for (const auto &c : node.children)
        collectSubtree(*c, out);
}

// Distinct, readable hues for auto-created rule groups, assigned round-robin.
QColor autoColor(int index) {
    static const QColor palette[] = {
        QColor(0xE6, 0x55, 0x4D), // red
        QColor(0x4D, 0x96, 0xE6), // blue
        QColor(0x5C, 0xB8, 0x5C), // green
        QColor(0xE0, 0x9F, 0x3E), // amber
        QColor(0x9B, 0x5D, 0xE5), // violet
        QColor(0x2E, 0xC4, 0xB6), // teal
    };
    constexpr int n = sizeof(palette) / sizeof(palette[0]);
    return palette[((index % n) + n) % n];
}

} // namespace

void resolveRuleGroups(const std::vector<const FsNode *> &roots, GroupStore &store) {
    // Anchors from *all* base surfaces in one pass (ADR-304), so resolving one base
    // never strands another's groups: an anchor is stale only if no root holds it.
    std::vector<const FsNode *> anchors;
    for (const FsNode *root : roots)
        if (root)
            findAnchors(*root, anchors);

    // Index anchors by key for quick lookup, and remember each node.
    QHash<MemberKey, const FsNode *> anchorByKey;
    for (const FsNode *a : anchors)
        anchorByKey.insert(keyFor(*a), a);

    // Drop rule groups whose anchor has vanished; refresh the survivors' members.
    QSet<MemberKey> resolvedAnchors;
    std::vector<QString> stale;
    for (const auto &g : store.groups()) {
        if (g->kind != GroupKind::Rule)
            continue;
        auto it = anchorByKey.constFind(g->ruleAnchor);
        if (it == anchorByKey.constEnd()) {
            stale.push_back(g->id);
            continue;
        }
        g->members.clear();
        collectSubtree(*it.value(), g->members); // exclusions preserved, applied by contains()
        resolvedAnchors.insert(g->ruleAnchor);
    }
    for (const QString &id : stale)
        store.remove(id);

    // Create rule groups for anchors that don't have one yet. Seed the colour index
    // past the survivors so a fresh anchor usually gets a distinct hue. Survivors
    // keep their original (creation-order) hues, so after a rescan that adds/removes
    // anchors a new group can still land on a colour a survivor holds — accepted as
    // cosmetic; exact distinctness can come later if it matters.
    int colorIdx = resolvedAnchors.size();
    for (const FsNode *a : anchors) {
        const MemberKey key = keyFor(*a);
        if (resolvedAnchors.contains(key))
            continue;
        Group *g = store.create(GroupKind::Rule, a->name, autoColor(colorIdx++));
        g->rule = GroupRule::GitWorktree;
        g->ruleAnchor = key;
        collectSubtree(*a, g->members);
    }
}

} // namespace core
