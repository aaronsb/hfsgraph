// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/groupstore_io.h"

#include "core/group.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

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
    QStringList sorted(keys.begin(), keys.end());
    sorted.sort(); // deterministic order → a sidecar diff reflects real changes, not hash churn
    QJsonArray arr;
    for (const MemberKey &k : sorted)
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
    const QString path = workspaceStorePath(rootAbsPath);

    // Merge-preserve, keyed by group id: seed from whatever is already on disk, then
    // overlay the in-memory store. A group the in-memory store has *dropped* (e.g. a
    // shallow scan didn't reach a nested repo's anchor, so resolveRuleGroups judged it
    // stale and removed it) is kept on disk instead of being silently erased — that loss
    // would be irreversible. QMap keeps the output ordered by id for stable diffs. Trade-
    // off: a group whose anchor is *genuinely* gone lingers (there is no group-removal UI
    // yet); explicit deletion + stale cleanup is a deferred follow-up (ADR-102 #15).
    QMap<QString, QJsonObject> byId;
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly)) {
            const QJsonDocument prev = QJsonDocument::fromJson(in.readAll());
            if (prev.isObject())
                for (const QJsonValue &v : prev.object()[QStringLiteral("groups")].toArray()) {
                    const QJsonObject o = v.toObject();
                    byId.insert(o[QStringLiteral("id")].toString(), o);
                }
        }
    }
    for (const auto &g : store.groups())
        byId.insert(g->id, groupToJson(*g)); // overlay live state over the on-disk copy

    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("workspaceRoot")] = rootAbsPath;
    root[QStringLiteral("workspaceId")] = workspaceId; // durable id if known, else empty
    QJsonArray groups;
    for (const QJsonObject &o : byId) // QMap iterates sorted by key (id)
        groups.append(o);
    root[QStringLiteral("groups")] = groups;

    // QSaveFile commits atomically (temp + rename), so a crash mid-write can't leave a
    // truncated sidecar that loadGroupStore would then drop as corrupt.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

bool loadGroupStore(GroupStore &store, const QString &rootAbsPath) {
    QFile f(workspaceStorePath(rootAbsPath));
    if (!f.open(QIODevice::ReadOnly))
        return false; // no sidecar for this workspace yet
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false; // corrupt / unexpected — leave the store untouched
    const QJsonObject root = doc.object();
    // Refuse a sidecar written by a newer format than we understand, rather than silently
    // mis-parsing it (and then overwriting it on the next save). Absent version ⇒ treat as
    // current (the field has existed since v1).
    if (root[QStringLiteral("version")].toInt(kFormatVersion) > kFormatVersion)
        return false;
    for (const QJsonValue &v : root[QStringLiteral("groups")].toArray()) {
        const QJsonObject o = v.toObject();
        // Additive merge: a group already in the store (by id) is live state that is at
        // least as fresh as disk (we save on every edit), so it wins — skip it. This lets
        // the loader run before every resolve without duplicating on repeated scans.
        if (store.find(o[QStringLiteral("id")].toString()))
            continue;
        store.adopt(groupFromJson(o));
    }
    return true;
}

} // namespace core
