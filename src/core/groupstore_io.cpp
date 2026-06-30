#include "core/groupstore_io.h"

#include "core/group.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace core {

namespace {

constexpr int kFormatVersion = 1;

// String forms for the GroupKind enum, so the JSON is human-readable and stable across
// enum reordering (we match on the string, not the ordinal).
QString kindToString(GroupKind k) {
    return k == GroupKind::Rule ? QStringLiteral("rule") : QStringLiteral("manual");
}
GroupKind kindFromString(const QString &s) {
    return s == QStringLiteral("rule") ? GroupKind::Rule : GroupKind::Manual;
}

// The only rule today (git-worktree); stringified for the same forward-compat reason.
QString ruleToString(GroupRule) { return QStringLiteral("git-worktree"); }
GroupRule ruleFromString(const QString &) { return GroupRule::GitWorktree; }

QJsonArray keysToJson(const QSet<MemberKey> &keys) {
    QJsonArray arr;
    for (const MemberKey &k : keys)
        arr.append(k);
    return arr;
}
QSet<MemberKey> keysFromJson(const QJsonArray &arr) {
    QSet<MemberKey> out;
    for (const QJsonValue &v : arr)
        out.insert(v.toString());
    return out;
}

QJsonObject groupToJson(const Group &g) {
    QJsonObject o;
    o[QStringLiteral("id")] = g.id;
    o[QStringLiteral("name")] = g.name;
    o[QStringLiteral("color")] = g.color.name(); // #rrggbb
    o[QStringLiteral("kind")] = kindToString(g.kind);
    o[QStringLiteral("exclusions")] = keysToJson(g.exclusions);
    // Rule-group members are recomputed by resolveRuleGroups on load, so they are not
    // persisted — only the anchor the rule resolves from. Manual groups have no rule, so
    // their curated member set IS the group and must persist.
    if (g.kind == GroupKind::Rule) {
        o[QStringLiteral("rule")] = ruleToString(g.rule);
        o[QStringLiteral("ruleAnchor")] = g.ruleAnchor;
    } else {
        o[QStringLiteral("members")] = keysToJson(g.members);
    }
    QJsonObject view;
    view[QStringLiteral("visible")] = g.view.visible;
    view[QStringLiteral("highlight")] = g.view.highlight;
    view[QStringLiteral("dim")] = g.view.dim;
    view[QStringLiteral("focus")] = g.view.focus;
    o[QStringLiteral("view")] = view;
    return o;
}

std::unique_ptr<Group> groupFromJson(const QJsonObject &o) {
    auto g = std::make_unique<Group>();
    g->id = o[QStringLiteral("id")].toString();
    g->name = o[QStringLiteral("name")].toString();
    g->color = QColor(o[QStringLiteral("color")].toString());
    g->kind = kindFromString(o[QStringLiteral("kind")].toString());
    g->exclusions = keysFromJson(o[QStringLiteral("exclusions")].toArray());
    if (g->kind == GroupKind::Rule) {
        g->rule = ruleFromString(o[QStringLiteral("rule")].toString());
        g->ruleAnchor = o[QStringLiteral("ruleAnchor")].toString();
        // members stay empty — resolveRuleGroups fills them from the current tree.
    } else {
        g->members = keysFromJson(o[QStringLiteral("members")].toArray());
    }
    const QJsonObject view = o[QStringLiteral("view")].toObject();
    g->view.visible = view[QStringLiteral("visible")].toBool(true);
    g->view.highlight = view[QStringLiteral("highlight")].toBool(false);
    g->view.dim = view[QStringLiteral("dim")].toBool(false);
    g->view.focus = view[QStringLiteral("focus")].toBool(false);
    return g;
}

QString dataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           QStringLiteral("/hfsgraph");
}

} // namespace

QString workspaceStorePath(const QString &rootAbsPath) {
    const QByteArray h =
        QCryptographicHash::hash(rootAbsPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return dataDir() + QLatin1Char('/') + QString::fromLatin1(h.left(16)) +
           QStringLiteral(".json");
}

bool saveGroupStore(const GroupStore &store, const QString &rootAbsPath,
                    const QString &workspaceId) {
    if (!QDir().mkpath(dataDir())) // XDG data dir, never the scanned tree
        return false;

    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("workspaceRoot")] = rootAbsPath;
    root[QStringLiteral("workspaceId")] = workspaceId; // durable id if known, else empty
    QJsonArray groups;
    for (const auto &g : store.groups())
        groups.append(groupToJson(*g));
    root[QStringLiteral("groups")] = groups;

    QFile f(workspaceStorePath(rootAbsPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool loadGroupStore(GroupStore &store, const QString &rootAbsPath) {
    QFile f(workspaceStorePath(rootAbsPath));
    if (!f.open(QIODevice::ReadOnly))
        return false; // no sidecar for this workspace yet
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false; // corrupt / unexpected — leave the store untouched
    const QJsonArray groups = doc.object()[QStringLiteral("groups")].toArray();
    for (const QJsonValue &v : groups)
        store.adopt(groupFromJson(v.toObject()));
    return true;
}

} // namespace core
