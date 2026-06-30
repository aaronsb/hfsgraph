// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// JSON persistence for the semantic group store (ADR-102 / task #15). A per-workspace
// sidecar in the XDG data dir (`~/.local/share/hfsgraph/<hash>.json`) keeps a group's
// colour, view state, exclusions, and (for manual groups) membership across sessions.
//
// Keying: the sidecar filename is a hash of the workspace root's *absolute path* — the
// store writes nothing to the scanned tree itself (consistent with the read-only scan,
// ADR-100; durable-id stamping stays on the commit-engine side). The root's durable id
// is recorded *inside* the JSON when the scan read one, so a later slice can match a
// moved workspace by id (orphan re-adoption, deferred).
//
// What persists: rule groups store anchor + exclusions + colour + view (their resolved
// members are recomputed by resolveRuleGroups on load, so they are NOT persisted);
// manual groups store their curated member set. Loading is additive (the loader adopts
// groups into the store); call it once per workspace before resolveRuleGroups so the
// rule engine reconciles persisted rule groups to the current tree by their anchor.
#pragma once

#include <QString>

namespace core {

class GroupStore;

// The sidecar path for `rootAbsPath` (…/hfsgraph/<first 16 hex of sha1(path)>.json).
// Pure — derives the path, touches no disk. Exposed for tests and existence checks.
QString workspaceStorePath(const QString &rootAbsPath);

// Write `store` to the sidecar for `rootAbsPath`, recording `workspaceId` (the root's
// durable id, or empty if unstamped) inside. Creates the XDG data dir if needed. Returns
// false on a write/serialisation failure. Overwrites any existing sidecar.
bool saveGroupStore(const GroupStore &store, const QString &rootAbsPath,
                    const QString &workspaceId);

// Load the sidecar for `rootAbsPath`, adopting its groups into `store` (additive — does
// not clear). Returns false if no sidecar exists or it can't be parsed (store untouched
// on parse failure). Rule groups arrive with empty members; the caller re-resolves them.
bool loadGroupStore(GroupStore &store, const QString &rootAbsPath);

} // namespace core
