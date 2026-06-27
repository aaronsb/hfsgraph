//! hfsgraph — a canvas tool for re-wiring a directory hierarchy to match its
//! semantic structure.
//!
//! This is a placeholder entry point. The first real milestone is a read-only
//! graph viewer (see CONCEPT.md "First pass (MVP)" and ADR-300): directories as
//! nodes, containment as edges, with the collapse/expand containment morph.
//!
//! Architecture decisions live in `docs/architecture/` (run `make adr CMD=list`).

fn main() {
    println!(
        "hfsgraph {} — scaffold only. The read-only viewer POC is the next milestone \
         (see CONCEPT.md and docs/architecture/).",
        env!("CARGO_PKG_VERSION")
    );
}
