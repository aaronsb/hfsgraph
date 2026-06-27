---
status: Accepted
date: 2026-06-26
deciders:
  - aaronsb
  - claude
related: [ADR-200, ADR-400]
---

# ADR-100: Durable directory identity via UUID-in-xattr with inode fingerprint

## Context

hfsgraph lets a user re-wire a directory hierarchy by drawing moves on a canvas,
buffering them in a ledger, and committing them as `mv` operations. Two capabilities
depend on being able to *name a directory independently of where it currently lives*:

1. **Semantic state** — a directory's marked purpose (container vs. leaf), its group
   membership (color + label), associations to other nodes, and notes — must persist in a
   sidecar state graph that survives the directory being moved, renamed, or re-nested.
2. **Commit-time safety** — before applying a buffered move we must confirm the node on
   disk is still the node we scanned ("we are moving what we think we're moving").

These are *two different jobs* and the obvious single answer — the inode — fails the
first one:

- **inodes are recycled** after deletion; a future directory can inherit a dead one's
  number.
- **inodes are per-filesystem, and per-subvolume on btrfs** (each subvolume is its own
  inode namespace), so an inode is not even globally unique on the target filesystem.
- **inodes are meaningless across a backup/restore or btrfs send/receive round-trip.**

So the inode cannot be the durable key for the state graph. But it *is* a good cheap
signal for the second job: `mv` within one filesystem preserves the inode, so
`(device_id, inode)` is a reliable "is this still the same object since I scanned it"
fingerprint within a session.

The target corpora are large and organically grown (e.g. `~/Projects` ≈ 6,000
directories), so whatever identity scheme we choose must not require mutating thousands
of pristine directories up front.

## Decision

Use **two distinct identities**, never conflated:

1. **Durable object id — a generated UUID stored in a `user.hfsgraph.id` extended
   attribute** on the directory. This is the key the sidecar state graph (purpose, group,
   associations, notes) joins on. `user.*` xattrs travel with the directory through `mv`
   and are preserved by xattr-aware copies (`cp -a`, `rsync -X`, btrfs send/receive).

2. **Runtime fingerprint — `(device_id, inode)` plus `mtime`/size**, recorded in memory
   at scan time and re-checked at commit time. Used only to verify a buffered op still
   targets the object we scanned. Never persisted as identity.

**Path is never identity** — paths are precisely the thing this tool churns.

**Lazy stamping.** The scan is strictly read-only: it builds the in-memory graph from
`path + (dev, inode)` and writes nothing. A UUID xattr is written only when a node is
*first touched* — moved, marked into a group, or given an association — never en masse.
The state graph only grows rows for nodes the user has given meaning to.

**Orphan re-adoption.** Because a non-xattr-preserving copy can strip the id, the state
graph also indexes `last-known-path + fingerprint`, so a node whose xattr id is missing
can be re-matched to its prior state entry instead of losing its meaning.

## Consequences

### Positive

- Semantic state survives arbitrary moves/renames/re-nesting — the whole point of the
  tool — because the key lives *with* the directory, not in its path.
- Commit-time verification is cheap and precise: an inode/mtime mismatch aborts before a
  wrong `mv` lands.
- Read-only scans mean pointing the tool at a 6,000-dir tree mutates nothing until the
  user acts; pristine directories stay pristine.

### Negative

- Requires a filesystem with `user.*` xattr support (btrfs qualifies; some targets may
  not), and write/ownership on a node to stamp it.
- Durable ids can be lost if directories leave the tool's care via xattr-dropping copy
  tools — mitigated, not eliminated, by orphan re-adoption.
- Two-identity model is more to implement and explain than "just use the inode."

### Neutral

- Establishes the sidecar state graph (SQLite or JSON) keyed on UUID as a core component.
- Cross-filesystem / cross-subvolume moves (copy+delete, inode changes) must be detected
  and handled as a special case by the ledger — tracked separately.

## Alternatives Considered

- **inode as durable id** — rejected: recycled, per-filesystem/per-subvolume, lost on
  restore. Retained only as the ephemeral fingerprint.
- **path as identity** — rejected: paths are the mutable thing under edit; any move would
  orphan the state.
- **content/structural hash of the subtree** — rejected as primary id: expensive on large
  trees, unstable (changes whenever contents change), and ambiguous across identical
  subtrees. May still serve as an optional integrity check.
- **A manifest file dropped inside each directory** (e.g. `.hfsgraph`) instead of an
  xattr — rejected as the default: pollutes the tree, shows up in listings and the very
  graph we render, and can collide with project contents. xattrs keep identity
  out-of-band while still traveling with the directory.
