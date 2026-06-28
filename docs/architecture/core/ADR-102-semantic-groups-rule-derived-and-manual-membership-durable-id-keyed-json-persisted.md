---
status: Accepted
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-101, ADR-100, ADR-200, ADR-301]
---

# ADR-102: Semantic groups: rule-derived and manual membership, durable-id keyed, JSON-persisted

## Context

ADR-101 decided that a richer many-to-many graph rides on top of the containment tree —
**semantic groups** (one node → many groups) and **associations** (node ↔ node with a
co-move policy) — carrying *behavioral policy, not data*, and that this overlay is app state
the filesystem cannot express. This ADR fixes the concrete model for the **groups** half: how
a group is defined, how its membership survives the directory moves the tool exists to
perform, where it is stored, and the first auto-derived group. (Associations and the explicit
co-move policy are deferred; groups plus the identity/persistence substrate they need come
first.)

The crux is movement. hfsgraph's purpose is re-parenting (`mv`, ADR-200), so any membership
that is stored by **path** breaks on the very operation the tool performs — every move would
have to rewrite group membership, and a missed rewrite silently corrupts a group.

The motivating case is a git working tree. A directory containing `.git` is a unit that must
move atomically: if a group captured only *tracked* files, moving it would strand the
*untracked* work, which quickly makes a mess. So the safe default for that group is
**inclusive** — everything under the repo root — with explicit, deliberate carve-outs.

## Decision

**Groups come in two kinds, and membership is keyed on durable identity, never path.**

- **Rule-derived groups** compute their membership from a rule over the tree, re-evaluated on
  every (re)scan. The first rule is **git-worktree**: the anchor is the directory that
  contains a `.git` entry; members are the anchor plus *all* descendants (files and
  directories, including `.git` itself), **minus explicit exclusions**. This is move-robust by
  construction — after a move the rule re-resolves to the same set — and new untracked files
  auto-join on rescan. The inclusive default is deliberate: moving the group never strands
  untracked work. Exclusions are explicit carve-outs and persist (keyed by durable id).
- **Manual groups** are an explicit member set the user curates ("these photos").

- **Identity.** Members and exclusions are referenced by durable identity, not path.
  Directories use ADR-100 durable directory identity (UUID in xattr + inode fingerprint).
  Files have no first-class durable id yet (ADR-100 is directory-scoped), so a file is
  referenced as `(containing-directory durable id + filename)`; revisit if/when files gain
  durable ids.

- **A group carries:** `id`, `name`, `color`, `kind` (rule | manual), the rule or the member
  set, `exclusions`, and **view state** (visible / highlight / dim / focus) that drives a
  canvas overlay over the treemap (ADR-301). A future `coMovePolicy` (ADR-101) will make
  moving a group move its members atomically — the *enforcement* of the untracked-work safety
  property above.

- **Persistence.** A per-workspace **JSON sidecar** in the XDG data dir
  (`~/.local/share/hfsgraph/<workspace-id>.json`), keyed by the workspace root's durable id.
  Human-readable and inspectable; it may migrate into the ADR-200 store later. Groups are app
  state and are **never written onto the filesystem objects themselves** — `user.xdg.tags` /
  Baloo interop (ADR-100/400) remains a separate, optional export bridge, not the store of
  record.

- **UI surface.** A left dock panel — a column of collapsible ("window-shade") group cards —
  serves as legend and control surface: define / edit / colour a group, and per group toggle
  visible / highlight / dim / focus / select-on-canvas, with the depth-ramp legend at the
  bottom. The panel's specifics are a UI concern (a later ui-domain ADR); this ADR fixes the
  model it manipulates.

## Consequences

### Positive

- Move-robust by construction for rule groups; durable-id keying means manual membership
  survives the tool's own `mv` operations — the one property a path-based store could not give.
- The git-worktree inclusive default directly prevents stranded-untracked-work: moving a repo
  group moves the whole working tree.
- Groups become the substrate for the deferred co-move/associations (ADR-101) and for
  drag-to-reparent (drag a group → move its members together).
- A JSON sidecar is trivial to build, inspect, and version, and pollutes nothing on disk.

### Negative

- Files lack first-class durable identity, so file membership keyed by `(dir id + name)` is
  fragile to in-place renames and to a file moving between directories *outside* the app.
  Acceptable until files gain durable ids.
- Two mechanisms to maintain (a rule engine and a manual store); rules must re-evaluate on
  rescan, including reconciling persisted exclusions against a changed tree.
- App-side storage means other tools (Dolphin/Baloo) don't see the groups unless the optional
  xattr export bridge is added later.

### Neutral

- Associations (node ↔ node) and the explicit co-move policy from ADR-101 are deferred; this
  ADR delivers the groups half plus the identity/persistence substrate both halves need.
- The group colour overlay composes with the ADR-301 depth ramp: highlight tints/borders a
  group's cells; focus dims non-members.

## Alternatives Considered

- **Path-based membership.** Rejected: breaks on the moves the tool exists to perform; every
  `mv` would have to rewrite membership.
- **Manual-only (no rules).** Rejected: the `.git` group would be a one-time snapshot that
  fails to track new untracked files — defeating the safety case.
- **`user.xdg.tags` / xattr as the store of record.** Rejected as primary: flat tags cannot
  carry rules, colour, or view state, and rules don't fit xattr. Retained as a possible
  optional export bridge (ADR-100/400).
- **SQLite now.** Rejected for the MVP: more plumbing than needed; revisit when the ADR-200
  ledger lands and a shared store earns its keep.
