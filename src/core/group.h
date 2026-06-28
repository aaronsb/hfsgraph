// Semantic groups (ADR-102): a many-to-many overlay on top of the containment
// tree (ADR-101). One node may belong to many groups; a group carries behavioral
// policy and view state, not data. This header is the in-memory model + store
// (Slice 1, task #1); rule resolution (task #2), the dock panel (task #3), the
// treemap overlay (task #4), and JSON persistence (Slice 4, task #15) build on it.
//
// Membership is keyed on *durable identity*, never path (ADR-102): a path-based
// store would break on the very `mv` operations the tool exists to perform.
// ADR-100 durable identity is not built yet (Slice 4 / task #14), so until then we
// key by absolute path — stable within a single scanned session. `MemberKey` and
// `keyFor()` are the single seam to swap when durable ids land.
#pragma once

#include <QColor>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

namespace core {

struct FsNode;

// Handle by which a tree node is referenced for membership. See the file note:
// path today, ADR-100 durable id (dir uuid + filename for files) later.
using MemberKey = QString;

MemberKey keyFor(const FsNode &node);

// A file has no FsNode of its own (ADR-102 keys it as containing-dir + filename).
// With path-based keys today this is just the file's absolute path.
MemberKey keyForFile(const FsNode &dir, const QString &filename);

enum class GroupKind {
    Rule,   // membership is *computed* from a rule over the tree, re-resolved on rescan
    Manual, // membership is an explicit user-curated set
};

// The rule a Rule-group derives its membership from (resolved by the rule engine,
// task #2). The first and only rule today is git-worktree: anchor = the directory
// containing a `.git` entry; members = anchor + all descendants − exclusions.
enum class GroupRule {
    GitWorktree,
};

// Per-group view state driving the treemap overlay (ADR-301/302). UI-facing, but
// kept on the group per ADR-102's "a group carries … view state".
struct GroupView {
    bool visible = true;    // group participates in the overlay at all
    bool highlight = false; // tint / border members in the group colour
    bool dim = false;       // de-emphasise this group's members
    bool focus = false;     // dim everything *except* this group's members
};

// One semantic group. Rule groups store their *resolved* member set (recomputed on
// rescan by the rule engine); manual groups store the curated set directly. Both
// honour explicit `exclusions` — the deliberate carve-outs from ADR-102's
// inclusive-by-default git-worktree case.
struct Group {
    QString id;   // stable within the owning store
    QString name; // display label
    QColor color; // overlay colour (composes with the ADR-301 depth ramp)
    GroupKind kind = GroupKind::Manual;

    QSet<MemberKey> members;    // manual: curated; rule: resolved
    QSet<MemberKey> exclusions; // explicit carve-outs (both kinds)

    GroupRule rule = GroupRule::GitWorktree; // meaningful when kind == Rule
    MemberKey ruleAnchor;                    // the rule's anchor (e.g. the repo root)

    GroupView view;

    // Effective membership: in the set and not carved out.
    bool contains(const MemberKey &k) const {
        return members.contains(k) && !exclusions.contains(k);
    }
};

// Owns the list of groups for one workspace. CRUD plus the membership query the
// overlay needs. No persistence yet (Slice 4 / task #15); no rule resolution yet
// (task #2 fills the resolved `members` of Rule groups).
class GroupStore {
  public:
    GroupStore() = default;

    // Create an empty group of the given kind; returns a stable pointer (owned by
    // the store). The id is auto-assigned ("g1", "g2", …).
    Group *create(GroupKind kind, const QString &name, const QColor &color);

    // Remove a group by id. No-op if unknown.
    void remove(const QString &id);

    Group *find(const QString &id);
    const Group *find(const QString &id) const;

    const std::vector<std::unique_ptr<Group>> &groups() const { return m_groups; }
    bool empty() const { return m_groups.empty(); }

    // Every group that effectively contains this key (members − exclusions). The
    // overlay uses this to decide a cell's tint / dim / focus state.
    std::vector<const Group *> groupsContaining(const MemberKey &k) const;

  private:
    std::vector<std::unique_ptr<Group>> m_groups;
    int m_nextId = 1;
};

// ---- Rule engine (task #2) ----------------------------------------------------
//
// The only rule today is git-worktree. (Re)resolve all rule groups over the tree:
//   * Every directory holding a `.git` entry (a subdir *or* a `.git` file, the
//     latter covering submodules / linked worktrees) is an anchor.
//   * The anchor's group members = the anchor + all descendant directories and
//     files (including `.git` itself) − the group's explicit exclusions.
// Inclusive by design (ADR-102): moving the group never strands untracked work.
//
// Idempotent and safe to call on every (re)scan: a rule group is matched to its
// anchor by `ruleAnchor`; its resolved `members` are recomputed while colour, id,
// view state, and exclusions are preserved. Anchors that have vanished have their
// rule groups removed; new anchors get a freshly-coloured rule group. Manual
// groups are never touched.
void resolveRuleGroups(const FsNode &root, GroupStore &store);

} // namespace core
