# hfsgraph — concept capture

A canvas tool for **re-wiring a directory hierarchy** the way qpwgraph re-wires audio
routes: boxes and edges for a real filesystem tree, with auto-layout, multiple
visualization modes, and a *propose → verify → commit* workflow over `mv`.

Mental lineage: **filelight** (proportional structure view) × **qpwgraph** (nodes,
ports, drawn routes) × **ComfyUI** (node canvas you edit) × **mermaid/d3** (tidy
auto-layout).

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
> canvas is `QGraphicsView` and layout comes from **Graphviz** (`twopi`/`sfdp`/`dot`) or
> **OGDF**/**igraph** (C/C++); treemap/sunburst are hand-rolled regardless.

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

**Immutable ≠ unselectable on the canvas.** Because layout position and filesystem
structure are *separate coordinate systems* (see below), the user can freely **reposition
an immutable node on the canvas** — drag it for readability, pull it near related nodes,
arrange the picture — because that's pure view state and touches nothing on disk. What's
forbidden is a structural change: re-parenting it (a `mv`). The canvas lets you move the
*box* around all you like; it just won't let you change what the box *means*.

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

## Two coordinate systems (layout vs. structure)

A node has two independent positions, and conflating them is the classic graph-tool trap:

- **Layout position** — where the box sits *on the canvas* (x/y, cluster, manual nudges).
  Pure **view state**, persisted in the app's own store, never on the filesystem. Editable
  for *every* node, including immutable ones. Changing it is free and reversible.
- **Structural position** — where the node sits in the *directory tree* (its parent).
  This is the real thing; changing it is a `mv` op that flows through the ledger → verify
  → commit. Forbidden for immutable nodes.

So a canvas drag is **two distinct gestures** that must be visually unambiguous:

- **drag-to-arrange** — drop into empty canvas space → updates layout only, no op.
- **drag-to-rewire** — drop *onto another node* (its container/port) → emits a `move` op
  into the ledger (or is rejected if the source is immutable / the drop is illegal).

This keeps "tidy up the picture so I can think" completely separate from "actually
restructure the filesystem" — you can rearrange a locked, read-only subtree into a legible
shape without ever risking a change to it.

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

## Visualization modes

One node graph, many renderers — current state solid, proposed state ghosted/overlaid
with diff edges:

- radial tidy tree
- force-directed tree
- tangled tree
- tidy tree
- sunburst
- nested treemap

Group color/label is a shared visual channel across all modes.

### Snap-to-physics layout (anti-hairball)

The force-directed mode needs a **"snap to physics" layout engine**, not a raw
spring-embedder that relaxes into a tangle. The default failure mode of force layouts at
scale (~6,000 nodes) is the **hairball** — everything overlapping, edges crossing, no
legible structure. Requirements:

- **Settles, then snaps.** The simulation runs to a low-energy state and then *quantizes*
  — nodes snap to a clean arrangement (grid, ring, or tier) rather than floating at
  organic-but-messy coordinates. The result reads as deliberate, not jittery.
- **Tree-aware forces.** Because the underlying graph is a strict containment tree (not an
  arbitrary DAG), the engine can exploit that: hierarchical/radial constraints + repulsion
  keep parent→child legible instead of letting containment edges tangle with everything.
- **Stable & incremental.** Adding a pending move or expanding a node should *nudge* the
  layout, not re-throw the whole board — important when the whole point is comparing
  current vs. proposed state without losing your visual bearings.
- **Collision/overlap resolution** so boxes never stack, and **edge-bundling or
  orthogonal routing** so the lines stay followable.
- **Pin & freeze.** A node the user has hand-placed (recall layout ≠ structure) stays put;
  physics flows around pinned anchors.

Conceptually closer to a constrained/quantized layout (cola.js-style constraints, or
Graphviz `sfdp` with post-snap) than to a naive `d3-force` blob.

### Design language (canvas aesthetic)

The canvas should feel like a deliberate **physical space**, not a node soup (detailed in
ADR-300):

- **Canvas themes:** dark / light / twilight, chosen independently of node styling.
- **Background with presence:** a rendered ground — grid lines, dots at intersections, or
  tiled polygons — not a flat fill, so panning/zoom feels spatial.
- **Nodes & edges float** above the canvas (drop shadows), with robust high-contrast colors
  so group/state reads against any background.
- **Not everything is a circle:** directory nodes are rectangular containers showing a file
  listing; shape carries meaning (container/leaf, group, state).
- **Reuse native Qt/KF6 widgets** for non-canvas chrome — lists, dialogs, toolbars — and
  minimize clutter; the canvas is the interface.
- **Native theming with canvas override:** as a Qt6/KF6 app (ADR-400) the chrome inherits
  the KDE color scheme and style automatically; the canvas takes that palette as its default
  and layers its own themes (dark/light/twilight) on top.

## First pass (MVP): a read-only graph viewer

Before any of the dangerous machinery (identity stamping, ledger, `mv`, groups, volumes,
immutability), build the thing that just *draws the tree as a graph* — read-only. At first
it "wouldn't look much different than a file-tree view," just expressed as nodes and edges
with a reasonable auto-layout. This de-risks everything: no writes, no commit, pure viewer.

- **Node = directory.** Each node is a box that lists the directory's **file contents**
  (a mini file-listing inside the node).
- **Edge = containment.** A parent→subdirectory relationship is an edge. Subdirectories
  are their own nodes.
- **Pan / zoom / drag-to-arrange.** Layout only (no structural meaning yet — see the two
  coordinate systems). A reasonable initial auto-layout, then the snap-to-physics engine.

### Collapse / expand = the containment morph (the core visual idea)

Containment renders **two ways**, and `[+]` / `[-]` morphs between them — this *is* the
"**nested treemap meets force-directed graph**" feel:

- **Expanded `[-]`** — children are **separate nodes**, joined to the parent by
  containment **edges**. The force-directed, qpwgraph-style picture.
- **Collapsed `[+]`** — the parent draws a **perimeter that swallows its descendants**,
  nesting them *inside* its own box. The containment edge becomes spatial enclosure. The
  nested-treemap picture.

Same relationship, two depictions; collapse/expand slides between them. This means
**"force-directed tree" and "nested treemap" stop being separate modes** and become two
ends of one interactive continuum — you can have part of the graph exploded into nodes
and part of it collapsed into nested boxes *at the same time*, per subtree.

This MVP is the foundation everything else snaps onto: once the canvas, layout, node
rendering, and collapse/expand work read-only, the ledger / verify / commit / semantic
layers are additive — and none of them can corrupt anything until they're built.

## Roadmap (committed next)

The POC is a read-only viewer. Status of the core items:

1. **Force-directed layout** — *done.* d3-style spring-electrical model (inverse-square
   charge with mass = files + child dirs, Hooke springs to a rest length, velocity damping,
   cooling alpha) that settles instead of ringing. **Physics on/off toggle** + live
   **repel/attract** controls done. **Box collision** done (cards separate on real bounds,
   no overlap). Remaining: **snap-to-physics** (settle → quantize) per ADR-300, and
   **Barnes-Hut** to drop the O(n²) per-tick cost at scale.
2. **Window-shade nodes** — *done.* Roll between a compact stats node (files · dirs · size)
   and a lazily-built file viewer that toggles icon-grid ↔ detail and is drag-resizable.
   Bulk controls (expand/shade all, icons/list, fit-to-count) done. Node size reflects
   object count via fit-to-count.
3. **Lazy expand** — *not yet.* Scan/expand subtrees on demand so the full ~6k-node tree is
   navigable without loading it all up front. (Pairs with Barnes-Hut for scale.)
4. **Nested containment morph** — *not yet.* Collapse `[+]` currently hides children; it
   should *swallow* them into a nested perimeter (treemap-in-graph), ADR-300's signature.
5. Then the mutating layers (ledger → verify → commit) on the Rust core (ADR-200/401).

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
