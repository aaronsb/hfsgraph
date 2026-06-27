---
status: Draft
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-100, ADR-101, ADR-300]
---

# ADR-200: Changeset legality and safety model: ledger, dry-run, volumes, immutability, commit

## Context

hfsgraph mutates a real filesystem via `mv`. These changes are consequential and
potentially irreversible. Editing the graph must therefore never touch disk directly;
instead it must accumulate intent that can be inspected, proven legal, and applied as a
single verified transaction — conceptually Terraform's plan→apply or KDE Partition
Manager's staged operations, but for filesystem *layout*. The model must also reason about
the messy realities of a real Linux filesystem: permissions, ownership, volume/subvolume
boundaries, read-only and non-POSIX mounts, symlinks, hardlinks, and git repositories.

## Decision

Adopt a **what-if-by-default, staged-transaction** model with an explicit, verified commit.

**Ledger (changeset buffer).** All edits accumulate as an ordered buffer of operations —
`move`, `mark_container`, `assign_group`/`unassign_group`, `link`/`unlink` (associations
with a co-move policy). Nothing touches disk until an explicit **Apply**. The graph the
user manipulates is the *proposed* tree; the real tree is read-only until commit.

**Dry-run legality (computable change set).** At any time the ledger can be *verified*
instead of committed: applied to a virtual clone of the tree and reported per-op as
legal/illegal with reason. Invariants:

- **No name collision** in the destination parent (no clobber).
- **No cycle** — a directory cannot move into its own descendant.
- **Permissions** — write+execute on the source's parent (to unlink) and on the
  destination parent (to link); sticky-bit parents additionally require ownership.
- **Immutability** — a node is `immutable` (never a move source) if it is on a read-only
  volume (inherited down the subtree), lacks move permission, or has the `chattr +i` flag.
  Causes are retained for explanation; the destination side is checked as the mirror case.
- **Volume boundaries** — a move whose source and destination-parent `device_id` differ is
  `EXDEV` (copy+delete, not rename), classified by cost: same-fs reflink copy vs.
  cross-filesystem full copy. Non-POSIX (`!posix_perms`) and `!supports_xattr` targets are
  flagged because they break ownership/permission preservation and identity (ADR-100).
- **Git boundaries** — moving a subdirectory *out of* a repo working tree, or relocating a
  submodule gitlink, is more than a `mv` and is flagged.

**Commit sequence.** (1) btrfs snapshot of each *touched* snapshot-capable subvolume as the
catastrophic-undo net (warn for touched volumes that cannot be snapshotted); (2) re-verify
every op's source against its recorded fingerprint — `(device_id, inode)` + mtime/size and
the `user.hfsgraph.id` xattr (ADR-100) — to ensure we move what we scanned; (3) apply moves
in dependency (topological) order, using temporary staging names to avoid transient
collisions; (4) rewrite symlinks per policy; (5) update the sidecar state graph with new
paths. Any mid-commit divergence aborts; the snapshot is the fallback.

## Consequences

### Positive

- No edit can corrupt the filesystem before Apply; the dangerous surface is one isolated,
  testable transaction gated by the dry-run.
- "Verify" gives the user a provable, explainable legality report before committing.
- Immutability and volume typing turn real-world hazards (read-only mounts, vfat, network,
  cross-subvolume) into visible, first-class constraints rather than runtime surprises.

### Negative

- The legality checker must model a lot of filesystem reality (permissions, EXDEV,
  sticky-bit, xattr/POSIX capability, symlinks, hardlinks, git) — significant complexity
  concentrated in one module.
- Snapshot coverage across multiple touched subvolumes complicates the undo guarantee.
- Topological application with staging names is fiddly to get right (swaps, chains).

### Neutral

- Requires a sidecar state graph (see ADR-100) and a virtual-tree model for dry-run.
- The engine is deliberately separable from the UI (a pure module over the model), enabling
  exhaustive testing and a possible future reuse as a CLI/library.

## Alternatives Considered

- **Apply each edit immediately** (no ledger) — rejected: removes verification, makes
  errors irreversible, and defeats the whole what-if premise.
- **Commit without snapshot/fingerprint re-verification** — rejected: the filesystem can
  drift between scan and apply; committing blind risks moving the wrong thing or clobbering.
- **Block all cross-volume / non-POSIX moves outright** — rejected: too restrictive; the
  right behavior is to classify and warn (copy+delete cost, dropped ownership) and let the
  user decide, not to forbid.
- **Rely solely on btrfs snapshot for safety** (skip the dry-run) — rejected: snapshots are
  a last resort, not a substitute for proving a change set legal before touching disk, and
  they don't cover non-snapshot-capable touched volumes.
