---
status: Accepted
date: 2026-08-23
deciders:
  - aaronsb
  - claude
related: [ADR-200, ADR-302, ADR-100, ADR-101, ADR-102]
---

# ADR-201: File operations on the ledger: tagged MoveOp, file identity via originalPath, immediate vs staged actions

## Context

ADR-200 fixed the engine model (a staged changeset, dry-run for legality, committed as
one verified transaction) and ADR-302 fixed the interaction on top of it, with ADR-101's
single structural operation: re-parenting a directory. The ledger, replay, verify and
queue dock were all built around that one verb, `MoveOp` = (source dir, destination dir).

The selection model (#37) then made individual *files* addressable on the treemap, and a
selection that can be made but not acted on is a dead end. Files need the same
plan-then-commit discipline as directories: rename, move into another directory, trash.
They also need a handful of actions that are not edits at all (open, reveal in the file
manager, copy the path) and should not be made to queue.

Issue #39 anticipated an `FsOp` variant with subject keys. Rather than design that on
paper, the file-ops spike (#38, PR #57, merged 2026-08-23) was built to find the shape,
reviewed, and remediated (commit 3257855). This ADR records what that spike proved, what
it left open, and what the apply engine (#16b) inherits.

## Decision

**1. One op type, tagged, not a variant.** `MoveOp` carries an `OpKind`
{`MoveDir`, `MoveFile`, `RenameFile`, `TrashFile`} plus the fields a file op needs
(`fileName`, `newName`); `destParent` is empty for a rename or trash. The four kinds
share one ledger, one replay step (`replayOne`, now the single implementation behind
both the projection and the legality pass), one verifier and one queue dock. The spike
showed the tagged struct carried through all of these without a case that wanted
per-kind storage or polymorphic dispatch; the `MoveDir`/file split is a two-way branch at
the three points that care (legality, apply-to-projection, verify). A variant is
reserved for the moment a kind needs state the others cannot hold.

**2. Subject model and identity.** A file op's subject is *(directory key, file name)*:
the `MemberKey` of the directory holding the entry (ADR-102's key scheme, path today,
durable id per ADR-100 later) plus the entry's name in that directory *as the projection
shows it at the time of staging*. Directory identity is carried by the key; file identity
across a chained plan is carried by `FileEntry::originalPath`, set by the scanner and
copied unchanged through the projection while staged renames and moves re-home the entry.
`replayVerdicts` replays the plan and reports, per op, the subject's on-disk origin at
that point; verify stats that origin. This is the fix for the spike's H1 finding: before
it, verify stat'd `dir/fileName` with the *projected* name, so a rename followed by a
move of the renamed entry was legal in replay and `SourceMissing` at verify. Files get no
durable id of their own; the directory's identity plus the scanned path is the anchor.

**3. Two action classes.** Actions on the selection are either

- *immediate* — Open, Open With… (the file manager's own application list via
  `KFileItemActions`), Reveal in file manager, Copy path(s): handed to KIO and done.
  These read the entry's scanned path, never the projected name. They are the actions
  that cannot change the tree; or
- *staged* — Rename…, Move to Trash, and move-into-directory by dropping the selection on
  a cell: a `MoveOp` appended to the ledger, shown in the projection, verified and
  committed with the rest of the plan.

`KPropertiesDialog` was in the spike's immediate set and is excluded: it renames, chmods
and chowns on the spot, writing to disk outside the ledger and past verify/commit. A
read-only properties view can return later; a writable one cannot. The rule the split
encodes: an action either cannot write, or it writes only through Commit.

A drop stages moves only when the drag carries hfsgraph's private mime type. A foreign
drop (a file from Dolphin) is ignored; import is not expressible on the ledger (see 6).

**4. Legality.** `checkFileOp` judges against whatever tree the nodes live in, the same
evolving-tree rule as `checkMove` (ADR-200 #16a): a chained plan is judged in apply
order, so op B is legal against what op A left. Verdicts reuse `MoveLegality`:

- no such entry in the holding directory → `SameNode` (the null verdict, also the verdict
  for a second op on an entry an earlier op trashed or moved away);
- `MoveFile` into the holding directory itself → `SameNode`; a file or subdirectory of
  that name already at the destination → `Collision`;
- `RenameFile` to an empty name, one containing `/`, `.` or `..` → `BadName`; to the same
  name → `SameNode`; to a name already taken in the directory → `Collision`;
- `TrashFile` is `Ok` once the entry resolves.

Staging judges at the ledger's end, scrubbing forward first, so an op legal against a
scrubbed-back view cannot be appended and silently skipped at replay. A refused stage is
reported to the operator (status bar). Files cannot cycle, so a file move into an
ancestor of its directory needs no cycle check.

**5. Verify.** For a file op whose replay verdict is `Ok`, `verifyPlan` checks:

- the entry still exists at its origin (`replayVerdicts` subject origin);
- the holding directory can be stat'd and its `(dev, inode)` matches the scanned
  fingerprint, otherwise `SourceMissing` / `SourceDrifted`;
- for `MoveFile`, the destination exists and shares the entry's device, otherwise
  `DestMissing` / `CrossVolume`.

Known gap: no per-file `(dev, inode)` is recorded at scan. A file replaced in place by a
different object between scan and commit passes verify as long as its directory is the
same inode. ADR-200's "move what we scanned" guarantee holds for directories and is
weaker for files. Recording a per-file fingerprint at scan is the obvious fix and is
deferred until the apply engine exists to consume it.

**6. Carried into the apply engine (#16b), undecided here.**

- *Three verbs.* Apply now maps `MoveDir`/`MoveFile` to `mv`, `RenameFile` to `rename`,
  `TrashFile` to `KIO::trash` (a trash, never an unlink). A copy verb exists only if
  drag-copy is wanted; nothing in this ADR needs it.
- *Weight before commit.* A staged trash or move removes the entry from the directory's
  `files` and subtracts its count and bytes, so the directory's weight changes and the
  map re-flows before anything is committed. The spike observed this; it is consistent
  with "the canvas shows the projection" (ADR-302) and disorienting when a large trash
  reshuffles the map. Whether staged ops freeze weights until commit is a UI decision
  for a later ADR in the 300 band, not settled here.
- *Import.* A foreign URL dropped on a cell cannot be expressed as an op on the scanned
  forest; it is dropped on the floor. Import is out of scope for this ADR.
- *Case-insensitive collisions.* `Collision` is an exact-string test. On a casefolded or
  case-insensitive target (vfat, some ext4 dirs, network mounts) `a.txt` and `A.TXT`
  collide on disk and not in the projection. Names with leading, trailing or only
  whitespace are accepted. Both belong with ADR-200's volume typing, where the target's
  capabilities are known.
- *Menu cost on large selections.* `KFileItemListProperties` sniffs a mime type per
  item on right-click and Open launches one viewer per URL; both need a cap.

## Consequences

### Positive

- Files and directories share one plan: one ledger, one replay, one verifier, one queue,
  one undo/redo. The spike added no parallel machinery and unified the two replay loops
  that previously risked diverging.
- A chained file plan (rename, then move, then trash) previews, verifies and will apply
  in the same order, with each op anchored to where the entry actually is on disk.
- The immediate/staged split keeps ADR-200's invariant — only Commit writes — checkable
  by reading the action list.

### Negative

- File identity is weaker than directory identity: a path under a fingerprinted
  directory, not a fingerprint of the file. Drift of a single file between scan and
  commit is undetected until the apply engine records per-file `(dev, inode)`.
- `MoveLegality` is now overloaded: `SameNode` means "no entry", "self-move" and
  "self-rename" for files and "same node" for directories. Legible in code, lossy in the
  status bar; a file-specific verdict set may be needed once the UI wants to say why.
- The `MoveOp` struct carries fields most kinds leave empty. Cheap today, and a
  maintenance cost each time a kind grows state.
- Staged trash re-flows the map before commit. Accepted for now, unresolved as UX.
- No Properties action, so permissions and ownership are not visible from hfsgraph until a
  read-only view exists.

### Neutral

- `verifyPlan`'s file branch is the contract the apply engine implements against; the
  verbs it needs are listed in Decision 6.
- Legality excludes what verify excludes: cross-volume, permissions, immutability and
  the target's name semantics wait on ADR-200's volume model for files as for
  directories.
- The drag that exports `text/uri-list` to other applications also carries the private
  mime type; the two coexist on one drag.

## Alternatives Considered

- **`FsOp` as a `std::variant` of per-kind structs** (the shape #39 anticipated).
  Not taken: the spike found no kind-specific state a tagged struct could not hold, and
  the ledger, queue dock and undo/redo are simpler over one type. Revisit when a kind
  needs its own payload.
- **Resolve a file op's on-disk path by walking the ledger prefix backwards** at verify
  (review option H1a). Rejected: re-derives at verify what replay already knows, and
  breaks as soon as two ops name the same entry under different projected names.
- **Record the scanned (dir, name) origin on the `MoveOp` at stage time** (H1b).
  Rejected: puts per-entry identity on the op rather than the entry, so a second op on
  the same file needs the first op's origin copied forward by the UI.
- **Per-file `(dev, inode)` fingerprint at scan, now.** Deferred: correct, but the cost
  lands on every scan of every file and nothing consumes it until #16b. The gap is
  recorded above.
- **Immediate (unstaged) rename and trash**, as file managers do. Rejected: the same
  reason ADR-200 rejected apply-per-edit; it would make file ops the one path that
  writes without verify.
- **Keep `KPropertiesDialog` as an immediate action.** Rejected: it writes.
- **Accept any `text/uri-list` drop as a move of the selection.** Rejected: the spike's
  H2 finding; a drop from another application moved whatever hfsgraph had selected.
