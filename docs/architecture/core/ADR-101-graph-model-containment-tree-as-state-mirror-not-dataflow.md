---
status: Draft
date: 2026-06-27
deciders:
  - aaronsb
  - claude
related: [ADR-200, ADR-300]
---

# ADR-101: Graph model: containment tree as state-mirror, not dataflow

## Context

hfsgraph presents as a node-graph canvas, which visually resembles the large family of
"graph-as-program / graph-as-dataset" tools — shader editors, Node-RED, n8n, ComfyUI.
Those are **flow models**: nodes are operations, edges carry data/parameters, and the
graph *executes* (evaluation order, typed ports, fan-in/out, cycles, a runtime). Adopting
that mental model by default would be a category error that would distort every downstream
decision (ports, cycles, an evaluation engine, edge typing).

We need to state explicitly what kind of graph this is, because it is simultaneously
*simpler* and *more complex* than a flow graph, and both halves matter.

## Decision

The core graph is a **strict containment tree that mirrors filesystem state** — it is not
a dataflow graph and there is no execution engine.

- **An edge means containment ("parent-of"), nothing else.** No data flows along it.
- **The structural graph is a strict tree:** every node has exactly **one** parent (no
  fan-in), and **no cycles are expressible.**
- **No ports, no parameter types, no evaluation order, no runtime.** The only structural
  operation is re-parenting, which is a `mv`.

Two surfaces make it *heavier* than a flow tool, and both are first-class:

1. **Nodes are live external objects, not config blobs** — each is a real directory with
   inode, owner, mode, xattrs, a volume descriptor, immutability state, possibly a git
   boundary (see ADR-100, ADR-200). The graph is bound to a stateful, mutable, consequential
   substrate; "running" it *mutates the real world* and must be verified against drift.
2. **A second, richer graph rides on top of the tree** — semantic groups (one node → many
   groups) and associations (node ↔ node with a co-move *policy*) form a genuine
   many-to-many overlay the filesystem cannot express. These overlay edges carry
   **behavioral policy, not data**, and can *generate* structural ops (a `nests-into`
   association emits a move).

The defining one-liner: **hfsgraph is graph-as-state-mirror, not graph-as-program.** Flow
tools *compute a result*; hfsgraph *reconciles a proposed structure against a live one.*

## Consequences

### Positive

- Eliminates a whole class of complexity up front: no evaluation engine, no port typing,
  no cycle handling, no scheduler. The complexity budget goes into node descriptors and the
  apply transaction (ADR-200) instead.
- The single-parent invariant makes legality checking and layout tractable and the data
  model small.
- Gives a clear borrow/reject rule for UI work: borrow the canvas *vocabulary* (node
  canvas, drag-to-connect, minimap, auto-layout, a staging buffer ≈ Node-RED's "deploy"),
  reject the dataflow *engine*.

### Negative

- Users arriving from ComfyUI/Node-RED may expect dataflow affordances (ports, wiring
  arbitrary edges) that we deliberately do not provide; the UI must make the containment
  semantics obvious.
- The "richer overlay graph" (groups/associations) reintroduces many-to-many edges, so the
  model is not purely a tree — that duality must be kept clear in code and UI.

### Neutral

- Establishes the overlay (groups + associations) as a separate layer from the structural
  tree, persisted in app/sidecar state rather than as filesystem structure.
- Layout position and structural position are distinct coordinate systems (see ADR-300).

## Alternatives Considered

- **Adopt a dataflow/node-editor model** (ports, typed edges, evaluation) as the existing
  libraries (litegraph, egui-snarl, QtNodes) encourage — rejected: it imposes machinery
  (cycles, scheduling, port types) that has no meaning for directory containment and would
  mislead users.
- **General DAG instead of a tree** — rejected: a directory has exactly one parent;
  multi-parent containment is not representable on the filesystem (hardlinks aside, which
  are handled as a special case in ADR-200), so a DAG would model a structure we can never
  commit.
- **Treat it as a pure tree with no overlay** — rejected: the entire purpose (ADR project
  thesis) is to express semantic relationships the single tree cannot, so the policy
  overlay is essential, not optional.
