#include "core/group.h"

#include "core/fsnode.h"

namespace core {

MemberKey keyFor(const FsNode &node) {
    return node.path; // TODO(ADR-100): durable id once the scanner stamps one (task #14)
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

} // namespace core
