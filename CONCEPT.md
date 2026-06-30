# hfsgraph — concept capture

A canvas tool for **re-wiring a directory hierarchy**: a **squarified treemap** of a real
filesystem tree — nesting *is* the parent→child relationship — with semantic level-of-detail
zoom, floating investigation *frames* (lenses over any subtree), and a *propose → verify →
commit* workflow over `mv`.

Mental lineage: **filelight / QDirStat** (proportional, nested structure view) × **qpwgraph**
(drag a route, stage it, apply) × **Terraform** (plan → verify → apply) × **KDE Partition
Manager** (a buffer of pending operations, one explicit commit).

> **Rendering note (read this first).** Early concept work imagined a **node-link** canvas —
> force-directed boxes joined by drawn containment edges (ADR-300). That was **superseded by
> the squarified treemap** (ADR-301): containment reads as *spatial enclosure*, not a drawn
> edge, which stays legible across thousands of directories instead of collapsing into a
> hairball. Investigation **frames/lenses** (ADR-303/304) provide the "open a subtree in its
> own floating view" capability the node-link mode was reaching for. This document has been
> updated to the treemap model; where older passages still say "node" read **treemap cell**,
> and "containment edge" read **nesting**.

## Why this exists (the thesis)

A hierarchical filesystem **forces a single rigid tree** onto your stuff: one location
per item, one organizing axis, chosen once and ossified. But that physical structure
"forces a view that a semantic understanding can't give you" — how you actually *think*
about your files is semantic and multi-dimensional, and it rarely matches the one tree
they happen to live in.

You can paper over the gap by **assigning abstract semantic labels** (tags, groups,
roles) — but as soon as you do, *that* structure "quickly gets difficult to visualize."
Tags are invisible; relationships between them are implicit; the mental model lives only
in your head.

hfsgraph closes the loop between the two structures. It makes the semantic layer
**visible** (groups, colors, associations drawn on a canvas alongside the real tree) and
then lets you **optimally re-align the physical hierarchy with the semantic structure** —
moving the filesystem toward the way you think, and keeping a semantic overlay for the
relationships a single tree can never express. The point isn't prettier folders; it's
*reconciling where things live with what they mean.*

## What kind of graph this is (and isn't)

hfsgraph *looks* like the whole "graph-as-program / graph-as-dataset" family — shader
editors, Node-RED, n8n, ComfyUI — but it isn't one. Those are **flow models**: nodes are
operations, **edges carry data/parameters**, and the graph *executes* (evaluation order,
typed ports, fan-in/out, cycles, a runtime). We have **both a simpler and a more complex
surface than they do, at the same time** — that tension is the whole identity of the tool.

**Simpler at the core (the structural surface).** Our primary edge is just **containment
— "parent-of."** No data flows along it; nothing executes. That constrains the graph far
below a flow DAG:

- it's a **strict tree** — every node has exactly **one** parent (no fan-in),
- **no cycles** are even expressible,
- **no port types, no parameters, no evaluation order, no runtime.**

An edge means "lives inside," full stop. Re-wiring is the *only* operation, and it's a
`mv`.

**More complex at the edges (the live + overlay surfaces).** Two things make it heavier
than any flow tool:

1. **Nodes are live external objects, not config blobs.** A Node-RED node is abstract; our
   node is a *real directory* with an inode, owner, mode, xattrs, a volume type, an
   immutability state, possibly a git boundary. The graph is bound to a **stateful,
   mutable, consequential substrate** — and committing **mutates the real world**
   (irreversible moves, permissions, snapshots), verified against reality that may have
   drifted since the scan. Flow tools "run" cheaply and repeatably; we *reconcile* and
   *apply once*, carefully.
2. **A second, richer graph rides on top of the tree.** Semantic groups (one node → many
   groups) and associations (node ↔ node with a co-move **policy**) form a genuine
   many-to-many overlay that the filesystem can't express — and those overlay edges carry
   *behavioral policy* that can **generate** structural ops (a `nests-into` association
   emits a move). So the flow-graph richness reappears, but as **policy edges, not data
   edges.**

So the right one-liner: hfsgraph is **graph-as-state-mirror**, not graph-as-program. The
flow family *computes a result*; we *reconcile a proposed structure against a live one.*

| | Flow tools (Node-RED, n8n, shaders, ComfyUI) | hfsgraph |
|---|---|---|
| What an edge means | data / parameters flowing | containment ("parent-of") |
| Core graph shape | arbitrary DAG (fan-in/out, cycles) | strict single-parent tree |
| Ports / types | typed, parameterized | none |
| Does it execute? | yes — a runtime evaluates it | no — it *applies* a move set |
| Nodes are | lightweight config | live FS objects (inode, owner, volume, git…) |
| "Deploy" analogue | activate/deploy the flow | **Apply** the ledger (mutates disk, verified) |
| Extra graph layer | — | groups + associations (policy edges, can emit moves) |

What this means for the build: **borrow the canvas vocabulary** (node canvas, drag-to-
connect, minimap, auto-layout, a staging buffer ~ Node-RED's "deploy") but **reject the
dataflow engine** (no port typing, no evaluation, no cycles). The complexity budget goes
into the *node descriptors* and the *apply transaction*, not into an execution model.

## Prior art — are we reinventing the wheel?

A survey of the adjacent landscape (2026). Short answer: **the bullseye is empty, but the
pieces mostly exist** — the novelty is the *synthesis*, and the labeling half is largely
already solved on Linux.

**No direct competitor.** Nothing pairs a node-graph canvas with real `mv` reorganization
and a staged commit. Searched thoroughly (graph file managers, visual reorganizers,
drag-on-canvas + commit + btrfs). Nearest analogies: **Spacedrive** (Rust file explorer
with tags + views, but a conventional explorer UI, no graph-`mv` canvas) and
**GitKraken/SmartGit** (drag-to-rebase on a *commit* graph with reset/apply — our exact
plan/apply UX, but for commits not files). Analogies, not competitors.

**The labeling half is already the wheel — interoperate, don't reinvent.** KDE **Baloo** +
the freedesktop **`user.xdg.tags`** xattr standard + the **`tags:/` KIO** virtual-folder
protocol already implement "assign abstract semantic labels, store durably in xattr,
browse by label" — and it's *already running on the target system* (Baloo indexing ~1.7M
files). So if the maybe-future "supply semantic labels" capability ever ships, hfsgraph
should **write user-facing labels to `user.xdg.tags`** (so Dolphin/Baloo/`tags:/` keep
working) and reserve its **private `user.hfsgraph.id` xattr only for what Baloo doesn't
do**: the durable per-*directory* UUID identity token and the graph's layout/color/group
state. (Refines ADR-100: private xattr for identity + app state; standard xattr for any
user-visible tags.)

*Interop is with the **standard**, not the **daemon**.* Baloo is widely disliked — but
that reputation is about its resource-hungry **content indexer**, not the tag store. Tags
live in the freedesktop `user.xdg.tags` xattr *on the files*; Baloo only provides a fast
*index* over them. So hfsgraph reads/writes those xattrs directly and works **whether or
not Baloo is running**: if it is, tag browsing is index-accelerated and hfsgraph becomes
the good UX that finally makes Baloo's tagging worth having; if it isn't, the tags are
still standard, portable, and fully functional. Never take a hard dependency on the
daemon.

**The thesis is ~35 years old.** Gifford et al., *Semantic File Systems* (SOSP 1991), is
the intellectual ancestor of "a filesystem forces one tree but people think
multi-dimensionally." Everyone since solved it with **virtual overlays** (saved searches,
Smart Folders, tag-FUSE mounts, `tags:/`). hfsgraph's distinct move is solving it by
**committing a better *real* tree**, not adding another virtual view on top of a bad one.

**Closest tools to learn from:**
- **QDirStat** — treemap + tree that actually *operates* on the filesystem, with **btrfs
  snapshot integration**; its ops are cleanup/delete, not move, but it's the nearest
  "visualize + act" precedent.
- **`organize`** (tfeldmann) and **`rnr`** — simulate/dry-run + undo semantics for file
  moves/renames (headless, rule-driven); good models for our ledger's simulate/undo.
- **Obsidian** graph view — the canvas UX north star (but a graph of note *links*, never
  issues an `mv`).

**Genuinely novel (being honest):** not the thesis (Gifford), not xattr tagging (Baloo),
not graph canvases (ComfyUI/Obsidian), not dry-run+undo for file ops (rnr/organize), not
treemaps or btrfs-snapshot-undo (QDirStat). The actual contribution is the **synthesis**:
(1) the graph canvas is the *editor of physical directory layout* — manipulating it emits
a ledger of `mv`s — not a read-only view or a notes graph; (2) **Terraform-style
plan→verify→apply for filesystem *layout*** (clobber/cycle/permission/cross-volume
legality as a dry-run gate), which no fs tool packages; (3) **durable per-directory UUID
identity** so graph/semantic state survives moves — the one small gap Baloo leaves. The
defensible one-liner: *QDirStat's fs-operating treemap + Obsidian's canvas + Terraform's
plan/apply, driving real `mv`s, layered on top of Baloo's existing tags.*

> ⚠ **Tooling caveat (no-webview constraint):** the survey surfaced the best-known graph
> libraries — d3-hierarchy, Cytoscape.js, Sigma.js, ELK, litegraph — but these are all
> **web/JS** and can't be used without an embedded browser, which is a hard non-goal.
> Treat them as **algorithm and UX references only**. For the Qt6/KF6 build (ADR-400) the
> canvas is `QGraphicsView`; the **squarified treemap layout is hand-rolled** (no graph-layout
> library needed once the node-link direction was dropped — ADR-301).

## The core loop

Three states, always:

1. **Current** — what's actually on disk right now.
2. **Proposed** — current + a buffered ledger of edits (drawn, not yet applied).
3. **Commit** — apply the ledger to disk; on success, proposed *becomes* current.

A "re-wire" of one relationship = one `mv` with **no clobber** and safety fences.
Because the graph makes nesting cheap to do by accident, every structural change is a
*managed, controlled event*, never an implicit drag-drop that silently overwrites.

**What-if is the default and dominant mode** (the KDE Partition Manager model). The
tool is *always* staging. Editing the graph never touches disk; it builds up a queue of
pending operations you can review, reorder, and revert freely. There is exactly one
deliberate, explicit, batched escape hatch — **Apply Pending Operations** — and until
you press it, nothing on disk has changed. The graph you see and manipulate is the
*proposed* tree; the real tree is read-only until commit.

## The ledger (changeset buffer)

Operations accumulate in an ordered buffer before touching disk:

- `move(node, new_parent, new_name?)` — the FS-mutating op
- `mark_container(node)` — declares a node carries its subtree as a unit
- `assign_group(node, group)` / `unassign_group(node, group)` — semantic tagging
- `link(a, b, policy)` / `unlink(a, b)` — relationship edges with a co-move policy
- (groups and associations are also cascade ops — see below)

At **any** time, the ledger is a **computable change set** that can be *verified*
instead of committed: dry-run it against a virtual clone of the tree and report, per
op, legal / illegal + reason. Illegal ops are flagged in the canvas, not silently
dropped.

### Legality invariants (the dry-run checks)

- **No name collision** in the destination parent (no clobber).
- **No cycle** — can't move a directory into its own descendant.
- **Ownership / permissions** — must be able to write source parent *and* dest parent;
  ownership and mode are preserved, never silently changed.
- **Cross-filesystem / cross-subvolume** moves flagged — these are copy+delete, not a
  rename: slower, inode changes, can partially fail. Treated specially, not blocked.
- **Source still matches expectation** — fingerprint check (see identity below).

### Commit sequence

1. **btrfs snapshot** of the workspace subvolume — the catastrophic-undo net.
2. **Re-verify** every op: the source on disk must still match the fingerprint we
   recorded at scan time (we are moving what we *think* we're moving).
3. **Apply moves in dependency order** (topological). May need temporary staging names
   to avoid transient collisions when two nodes swap or chain through one parent.
4. **Rewrite symlinks** (see below).
5. **Update the state graph** with new paths.

If anything diverges from expectation mid-commit → stop, report, snapshot is the fallback.

## Durable identity — the part you were unsure about

You reached for "inode," and that's the right instinct for *one* of the two identities
you need — but not for the durable one.

**Two distinct identities, do not conflate them:**

| | What it is | Stable across `mv`? | Use it for |
|---|---|---|---|
| **Runtime fingerprint** | `(device_id, inode)` + `mtime`/size | within same fs: yes; across fs: **no** | *verification* at commit — "is this still the thing I scanned?" |
| **Durable object id** | a generated **UUID** stamped into the directory's metadata | yes, and survives across filesystems | the **key** your state graph joins on |

Why inode is wrong as the *durable* id:
- Recycled after deletion — a future dir can get a dead one's number.
- Per-filesystem (and per-**subvolume** on btrfs — each subvolume is its own inode
  namespace), so not globally unique on the very FS you target.
- Meaningless after a backup/restore or snapshot round-trip.

**The elegant durable id: a UUID in an extended attribute** (`user.hfsgraph.id`).
- xattrs in the `user.*` namespace **travel with the directory** through `mv`, and are
  preserved by xattr-aware copies (`cp -a`, `rsync -X`, btrfs send/receive).
- btrfs supports them natively.
- Your external **state graph** (a sidecar DB — SQLite or JSON) keys on this UUID and
  stores purpose/role, group membership, color, label, notes — all *independent of where
  the directory currently lives*.
- Caveat to design around: a non-xattr-preserving copy can strip the id. Mitigation:
  also index `path + fingerprint` so an "orphaned" node can be re-adopted, and only
  trust `user.*` writes where we own/can-write the node.

So: **inode = ephemeral fingerprint for safety checks; UUID-in-xattr = durable identity
for the state graph.** Path is never identity here — paths are the thing being churned.

**Lazy stamping (read-only scan).** Targets are large and organic — `~/Projects` alone
is ~6,000 directories. The scan must be **read-only**: it builds the in-memory graph
from `path + (dev, inode) fingerprint` and writes *nothing*. A UUID xattr is stamped
only when a node is **first touched** by an op, marked into a group, or given an
association — never en masse at scan time. Untouched directories stay pristine, and the
sidecar DB only grows entries for nodes you've actually given meaning to.

## Semantic groups (the "mark its purpose" idea)

- Right-click a directory → **mark as container**. Cascade-follow every descendant and
  tag the whole set into a **semantic group** (a `group_id` → {color, label}) written to
  each member's state (UUID-keyed; optionally mirrored into xattr).
- A node can be removed from a group, or reassigned to another.
- Once a set is marked, an **auto-reconnect** action can batch-generate `move` ops to
  relocate the whole group to a target — which flows right back into the ledger →
  verify → commit loop.

This gives the "poke around, assign meaning, then let the tool do the moves" workflow.

## Associations (the `.foo` ↔ `foo` model)

"Hidden moves with non-hidden" is **not** a hardcoded filesystem rule — that was too
narrow. The real model: a **relationship between two nodes is a first-class edge** in
the state graph that *you* assert, carrying a co-move policy.

`./foo/` and `./.foo/` may be completely independent in *purpose* yet *related*, and the
structure you actually want might be `./foo/.foo/` — i.e. the relationship can imply a
*re-nest*, not just "drag along." So associations support:

- **link(a, b)** — declare a relationship (with an optional label).
- **co-move policy** — when one endpoint moves, the other: `follows` (stays a sibling),
  `nests-into` (becomes a child of the other), or `independent` (link is informational
  only, no auto-op).
- A `co-move`/`nests-into` association *generates* its own `move` op into the ledger when
  its partner moves — flowing back through verify → commit like any other op.

Hidden children that live *inside* a moved directory still come along for free (they're
inside it); associations are for **siblings or cross-tree pairs** that the filesystem
wouldn't couple on its own. Group membership is "many nodes, shared meaning";
associations are "this node relates to that node, with a movement contract."

## Tagging subgraphs — a categorical lens (functors)

A useful way to think about tagging — not a formalism we have to implement, but a lens that
yields concrete invariants. Treat the directory tree as a small **category**: objects are
nodes, morphisms are containment paths (ancestor→descendant), composable and with
identities. A **semantic group / subgraph tag is then a structure-preserving map (a
functor)** from a sub-region of the physical-containment category into a semantic category
(your mental model of roles/labels).

What that lens buys us, concretely:

- **A subgraph tag is a *containment-closed sub-structure*, not an arbitrary node set.** The
  principled default: tagging a node tags a coherent region (it and the descendants it
  carries), so the tag composes with containment. "Mark as container → cascade to the
  subtree" *is* the functor — it maps the whole substructure coherently.
- **Functoriality = the co-move / cascade guarantee.** Because the map preserves composition
  and identity, moving a tagged container moves its mapped substructure *as a unit*, and
  relationships are preserved by construction. This is the formal backbone of
  `mark_container` and the association co-move policies.
- **The overlay is exactly where the tree can't keep up.** A node belonging to two groups,
  or an association that wants a node under two parents, is a place where the *semantic*
  category has morphisms the single-parent **tree cannot represent**. Category theory makes
  precise *why* the groups/associations overlay (ADR-101) must exist: it carries the
  semantic structure the physical functor can't land in the tree.
- **Current → Proposed as a natural transformation.** The two states are two functors from
  node-identity to (parent, position); the coherent morph between them — the diff you verify
  and commit — behaves like a natural transformation. A *legal* change set is one where that
  transformation respects the structure (no cycle, no collision); an *illegal* one is where
  naturality breaks.

So the whole tool, in this lens, is: **find and realize a functor that re-lands your
semantic structure onto the physical tree**, keeping an overlay for the morphisms the tree
can't hold. (Refines the graph model in ADR-101; informs the group/association data model.)

## Volume boundaries (first-class, *typed* node property)

The scan **traverses nested volumes by default** — pick a top-level directory and the
walk descends straight through any subvolume/mount it hits rather than stopping. But each
boundary is recorded as a **first-class, typed descriptor** on the directory where it
occurs, because crossing it changes identity, move semantics, and safety all at once.

### Detection and the descriptor

A directory is a **volume boundary** when its `st_dev` differs from its parent's. (On
btrfs *every* subvolume gets its own `st_dev` even on the same physical disk, so this
catches subvolumes, bind mounts, network shares, and separate filesystems alike.) At each
boundary we attach a descriptor built by **merging two sources of truth**:

- **`/etc/fstab`** — declared *intent* and persistent options: `ro`, `_netdev`, `nofail`,
  `noauto`, subvol name, the filesystem `UUID=`.
- **live mount table** (`findmnt --real` / `/proc/self/mountinfo`) — *reality*: actual
  `fstype`, source device, and btrfs `FSROOT`/subvol that fstab may not spell out, plus
  mounts not in fstab at all (automounts, removable, systemd units).

Neither alone suffices — fstab knows intent but misses live/removable mounts; the live
table knows reality but not `noauto`/declared options. Merge keyed on mount point.

Descriptor fields: `mount_point`, `fstype`, `source_device`, `fs_uuid`, `subvol`,
`mount_options`, plus a derived `classification` and `capabilities`.

### Volume taxonomy (the "what type is it" enumeration)

Classified from `fstype` + source + options, grounded in the real target system:

| Class | Example on this host | How a move there behaves |
|---|---|---|
| **same subvolume** | within `@projects` | true `rename(2)` — instant, inode preserved |
| **sibling subvol, same fs UUID** | `@projects` → `@docker`/`@scratch` (all `b82b51e3`) | `EXDEV`, but **reflink/CoW** copy+delete — near-instant, space-shared |
| **diff filesystem, same disk** | another fs on the same NVMe | copy+delete, real data movement |
| **diff physical disk** | `/data/1` (nvme0), `/data/2` (nvme3) | copy+delete, full data movement |
| **non-POSIX fs** | `/efi` (vfat) | **no xattrs, no ownership/perms** — identity + perm rules break |
| **network** | nfs/cifs/sshfs (`_netdev`) | slow; xattr/ownership preservation unreliable |
| **ephemeral** | tmpfs | lost on reboot — hostile move target |
| **read-only / removable** | `ro` mounts, USB | can't be a target / may vanish |

### Capability flags (derived from type, consumed by the rest of the system)

`supports_xattr` · `posix_perms` · `snapshot_capable` · `reflink_capable` · `read_only`
· `network` · `ephemeral` · `removable`. These are not cosmetic:

1. **Identity (ADR-100).** UUID-in-xattr identity *requires* `supports_xattr`. On a vfat
   `/efi`-class target it cannot work — must fall back or refuse, not silently lose ids.
2. **The "respect ownership/permissions" requirement** is unsatisfiable on `!posix_perms`
   volumes — a move onto vfat/exfat drops ownership and mode. Must be flagged, not done
   silently.
3. **Move cost & method** come straight from the table above: `rename` vs `reflink
   copy+delete` vs `full copy+delete`. The legality/preview layer shows the user which
   tier each pending move falls into.
4. **Snapshot safety has a hole across boundaries.** A btrfs snapshot covers *one*
   subvolume (and only on `snapshot_capable` volumes — not vfat/network/tmpfs). A move set
   spanning boundaries needs a snapshot per *touched* snapshot-capable subvolume, and an
   explicit warning for any touched volume that can't be snapshotted at all.

## Immutability (a derived, displayed node property)

Several unrelated conditions all reduce to one fact the user cares about: **this node
cannot be moved.** Rather than surface each cause separately, the scanner derives a single
**`immutable`** property (with the *cause* retained for explanation) and marks the node —
a lock badge on the canvas. An immutable node can never be a move **source**, and the
dry-run rejects any pending op targeting one *up front*, with the reason shown.

**Immutable ≠ uninspectable.** An immutable node can never be a move *source*, but it stays
fully explorable: open it in an investigation **frame** (ADR-303/304), highlight it via a
group, zoom into its contents — all pure view state that touches nothing on disk. What's
forbidden is the one structural change, re-parenting it (a `mv`). You can look at a locked
subtree however you like; the tool just won't stage a move out of it. *(Immutability
classification ships with the apply half, ADR-200.)*

Enumerable causes (extensible):

- **Read-only volume** — the node lives on a `read_only` mount (`ro` in fstab/options).
  Everything under it is immutable by inheritance.
- **No permission to move** — moving requires **write + execute on the source's parent
  directory** (to unlink the entry); a sticky-bit parent (`+t`, like `/tmp`) additionally
  requires that *we own the node*. If those aren't met, it's immutable. (Note the Linux
  subtlety: it's parent-dir writability, not ownership of the object itself, that
  normally governs a move — the property records which specific check failed.)
- **Filesystem immutable attribute** — `chattr +i` (`lsattr`), where the kernel itself
  forbids rename regardless of permissions.
- **(implied) destination not writable** — the *target* side of a move is the mirror
  case: a read-only or unwritable destination parent makes the move illegal even when the
  source is fine. Tracked by the dry-run, distinct from source immutability.

Immutability is **inherited down a subtree** when its origin is the volume (a whole `ro`
mount is frozen) and **per-node** when its origin is permissions or the `+i` flag. Marking
it at scan time means the canvas can grey out what can't move *before* the user wastes a
gesture drawing an edge that could never commit.

## Position is structure — except for frames (view state)

A classic graph-tool trap is conflating *where a box sits on the canvas* with *where it
sits in the tree*. The treemap sidesteps it for the base surface and reintroduces a clean,
bounded version of it only for frames:

- **Base treemap — position *is* structure.** A cell's location and size are computed by
  the squarify layout from the tree itself (nesting = parent→child, area = size/count).
  There is no free x/y to drag; the only thing a gesture on a cell does is **stage a move**
  — drag a cell onto another (ADR-302) → a `move` op into the ledger (or a rejection if
  illegal / the source is immutable). "Tidying the picture" isn't a separate mode here
  because the picture *is* the structure.
- **Frames — free position, pure view state.** An investigation frame/lens (ADR-303/304)
  floats at an arbitrary canvas position you drag and resize freely; that placement is view
  state (the app's own store, never the filesystem). Frames are how you "arrange the picture
  to think" — open several subtrees as floating lenses, place them side by side — while the
  base tree underneath stays untouched.

So the two gestures stay visually unambiguous: **drag a treemap cell onto another** = stage
a `mv`; **drag a frame's title bar** = reposition a lens (view only). You can frame and
arrange a locked, read-only subtree freely without ever risking a change to it.

## Git-aware grouping (special-case convenience)

A git repository is a **natural semantic-group boundary** — its working tree is a unit
that should almost never be split by accident. The scanner special-cases it:

- **Detect a repo** by `.git`. Note `.git` can be a *directory* (normal repo) or a
  *file* (a submodule or linked worktree pointing elsewhere) — both are signals.
- **Auto-propose a group** for the repo's working tree. This is a **recommendation, not
  an action** — surfaced in the canvas, accepted or dismissed. (At `~/Projects` scale,
  where most `app/*` entries are repos, this means the tool proposes dozens of groups up
  front instead of making you hand-mark each one.)
- **Submodules:** if `.gitmodules` defines submodules, their checked-out paths are part
  of *this* repo's tree. The proposed group **cascades to include the submodule
  subtrees**, so the repo + its submodule content tag as one group.

Move-safety consequences (these feed the legality checker, ADR territory):

- Moving a repo's working tree **as a whole** is fine — it's an ordinary `mv`.
- Moving a **subdirectory out of** a repo's working tree means ripping tracked content
  out of version control — **flag it** as a boundary-crossing move, don't do it silently.
- A **submodule directory is a gitlink**, not just a folder: relocating it touches the
  superproject's index and `.gitmodules` paths, which a plain `mv` won't fix — flag as
  more-than-`mv`.

## Visualization: one squarified treemap, semantic LOD

There is **one** renderer — a **squarified treemap** (ADR-301, Bruls/Huizing/van Wijk) —
not a menu of node-link layouts. Nesting is containment; cell area encodes a size metric
(file count or bytes); a depth ramp colors nesting level. This was chosen over the
force-directed node-link canvas (ADR-300) precisely because it stays legible at ~6,000
directories, where a spring layout collapses into a hairball.

What replaces "multiple modes" is **semantic level-of-detail**, driven by zoom plus two
independent controls:

- **Reveal** — how deep the subdivision goes (when a cell splits into its children).
- **Detail** — when a cell crosses from a solid block to showing its *contents*.

A cell's contents render as **rungs** chosen by available size (or forced from the toolbar):
solid tile → pixel **dots** (one per file, type-colored) → **icons** → a multi-column
**list** (`ls -a`) → **details** (`ls -l`: perms, size, mtime, symlink). One file-type color
dictionary is shared across every rung.

**Investigation frames (ADR-303/304)** are the "open this elsewhere" mechanism: double-click
a cell to float a **frame** — its own treemap rooted at that subtree, drawn at a
zoom-independent size (a magnifying lens), anchored back to its origin by a **callout**
(filled frustum / lines / off). Frames nest (a frame opened from within a frame), raise on
click, and close as a cascade. Every surface is the same `FrameItem` — the level-0 *base*
frames (one per scanned tree; several coexist) and the level-1+ lenses — so there is one
surface abstraction and one render path top to bottom.

Overlays compose on any surface: the **group overlay** (ADR-102 — highlight tint/border,
focus-dim non-members, de-emphasize, in the group color) and the **move diff overlay**
(ADR-302 — an amber crosshatch + step badge on every cell the staged plan relocates).

### Design language (canvas aesthetic)

The canvas should feel like a deliberate **physical space**, not a flat fill (ADR-301/304):

- **Background with presence** — a rendered ground (grid / dots / tiles), not a flat fill,
  so pan/zoom feels spatial.
- **Device-space chrome** — frame headers, resize grips, callout dither, and the
  constant-size labels are drawn in *screen* space, so they stay crisp and legible at any
  zoom while the treemap cells scale.
- **Ordered-dither textures** — drop shadows and callout frustums use tiled ordered dither
  (pixel-perfect, area scales with zoom) for a tactile, non-flat feel.
- **Shape carries meaning** — a cell is a rectangular container showing a file listing;
  group color, depth, and staged-move state read as overlays on it.
- **Native theming** — as a Qt6/KF6 app (ADR-400) the chrome inherits the KDE color scheme;
  the treemap layers its depth ramp and group colors on top.

## How the read-only viewer came together (the foundation)

Before any disk-mutating machinery (identity stamping, ledger, `mv`, groups, immutability),
the first build was a **read-only viewer** — draw the scanned tree, no writes, no commit.
That de-risked everything: the canvas, layout, content rendering, and navigation had to feel
right read-only before the mutating layers (groups → move staging → verify → commit) were
layered on as additive slices, none able to corrupt anything until built.

The original concept imagined a node-link viewer with a `[+]/[-]` **collapse/expand morph**
between a force-directed picture and a nested-treemap picture — two depictions of containment
on one interactive continuum. In practice the treemap won outright (ADR-301): rather than
morph between two layouts, **semantic LOD** (reveal/detail + zoom) slides continuously
between "a subtree is one solid block" and "a subtree is fully subdivided showing its files"
— the same continuum the morph was reaching for, but without a second layout engine or the
hairball failure mode at scale. **Frames** (ADR-304) give you the "explode this part out into
its own view" capability the expanded node-link picture promised.

## Status (what's built)

The read-only viewer grew into most of the propose → preview → verify loop. Shipped:

1. **Squarified treemap viewer** (ADR-301) — semantic LOD (reveal/detail + zoom); the
   file-content rungs (dots / icons / list / details); depth-ramp + file-type colors;
   outlier-robust name sizing; a large bounded canvas; **threaded scan** so a big/cold tree
   never freezes the UI.
2. **Frames as the universal surface** (ADR-303/304) — resizable floating lenses with
   per-level scan depth, zoom-from callouts, cardinality-1 re-raise, recursion +
   close-cascade; level-0 *base* frames support **multiple coexisting scanned trees**.
3. **Semantic groups** (ADR-102) — git-worktree rule groups (anchor + descendants −
   exclusions, re-resolved on rescan); a groups table with bulk highlight / focus / dim /
   show; and **JSON persistence** (a per-workspace XDG sidecar) so group color/view survive
   a restart.
4. **Move staging** (ADR-302) — drag a cell onto another → a legality-checked `move` into an
   ordered **ledger**; a bottom **queue dock** to scrub / undo / redo; an amber **diff
   crosshatch** marking every relocated cell; cross-frame drag with top-layer hit-testing.
5. **Durable identity** (ADR-100) — the scanner reads a `user.hfsgraph.id` xattr + a
   `(dev, inode)` fingerprint per directory (read-only); `keyFor` resolves to the durable id
   so groups/moves survive a directory being moved or renamed.
6. **Commit — verify half** (ADR-200) — a **Verify** dry-run proves the staged plan against
   the *current* disk per op (structural legality via ordered replay; source identity/drift
   vs. the recorded fingerprint; cross-volume `EXDEV` flag). Reads only.

**Next:** the commit **apply** half (ADR-200) — btrfs snapshot, re-verify, topological `mv`
with staging names, durable-id stamping, rollback — the one irreversible step, gated behind
the verify above. Then symlink / git-boundary classification, and eventually the Rust core
(ADR-401).

## Sharp edges to resolve (open questions)

1. **Symlinks.** Relative links pointing *inside* a moved subtree stay valid for free;
   **absolute** links and links crossing the move boundary need rewriting. Hardlinks:
   moving one name doesn't disturb the other — flag when a moved file has links *outside*
   the moving set. Need a policy per case.
2. **Subvolume boundaries.** A btrfs snapshot is itself a subvolume; moves across
   subvolume lines are copy+delete. How aggressively do we warn vs. just handle?
3. **Stack/runtime.** *Resolved (ADR-400):* standalone Qt6 + KDE Frameworks 6 (C++),
   `QGraphicsView` canvas, no embedded webview. Native KDE theming; `KF6::Solid`/`Baloo` +
   `libbtrfsutil` + `<sys/xattr.h>` for the engine.
4. **Rust core boundary.** *Resolved (ADR-401):* researched both in-process (cxx/cxx-qt) and
   out-of-process (stdio/JSON-RPC) options. Decision: **Rust core in-process via `cxx` +
   Corrosion**, with a message-shaped boundary that stays promotable to out-of-process later;
   transactional safety (WAL + snapshot + idempotent replay) lives in the core regardless of
   topology. The `core/` ↔ `ui/` split in the POC already embodies the seam.

## Worked example: `~/Projects` (the real mess)

The intended test corpus — ~6,000 dirs, 25 top-level buckets, organically grown. It
hands us the two flagship demos on the first probe:

- **`apps/` → `app/` merge.** `apps/` holds a single child (`tmux-menu`) while `app/`
  holds 29. Draw one edge `apps/tmux-menu → app/`, verify (no collision → legal), then
  prune the emptied `apps/`. One move, the whole point of the tool.
- **The `obsidian-*` semantic group.** `app/` contains `obsidian-3d-viz`,
  `obsidian-confluence-sync`, `obsidian-docxer`, `obsidian-mcp-plugin`,
  `obsidian-releases`, `obsidian-to-confluence`, … — an implicit group begging to be
  marked, colored, and auto-reconnected under a new `app/obsidian/` container.

Scale notes that feed the design: fan-out is wildly uneven (29 down to 0 — `data/` is
empty; `apps/`, `docker/`, `victron/`, `Steam/` are singletons), which makes those the
natural "should this even be top-level?" candidates; and at thousands of nodes the
renderers need collapse/focus + lazy expansion rather than draw-everything.
