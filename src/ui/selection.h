// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// The file selection, shared by every surface (#37). Files are keyed by (directory
// key, file name) — the same identity groups use (ADR-102) — so a selection survives a
// rung change, a re-layout, a lens over the same directory, and a projection copy.
// Plain click sets, Ctrl toggles, Shift extends from the anchor within one directory,
// a rubber band adds. Pure model; the scene owns it and repaints on `changed`.
#pragma once

#include "core/group.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <vector>

namespace core {
struct FsNode;
}

namespace ui {

class Selection : public QObject {
    Q_OBJECT
  public:
    explicit Selection(QObject *parent = nullptr);

    bool empty() const { return m_byDir.isEmpty(); }
    int count() const;
    bool contains(const core::MemberKey &dir, const QString &name) const;
    // The selected names in a directory, or null when none are — so a painter pays
    // one lookup per cell and nothing per glyph in unselected directories.
    const QSet<QString> *namesIn(const core::MemberKey &dir) const;

    void clear();
    void set(const core::FsNode &dir, int index);     // plain click: only this file
    void toggle(const core::FsNode &dir, int index);  // Ctrl-click
    void rangeTo(const core::FsNode &dir, int index); // Shift-click: anchor..index
    void add(const core::FsNode &dir, int index);     // one file
    void addAll(const core::FsNode &dir, const std::vector<int> &indices); // band: one signal

    // file:// URLs of the selection, for drag-out (text/uri-list).
    QList<QUrl> urls() const;

  Q_SIGNALS:
    void changed();

  private:
    void insert(const core::FsNode &dir, int index);
    void setAnchor(const core::FsNode &dir, int index);

    QHash<core::MemberKey, QSet<QString>> m_byDir; // dir key → selected names
    QHash<core::MemberKey, QString> m_dirPath;     // dir key → on-disk path (for urls)
    core::MemberKey m_anchorDir; // Shift-range anchor: by name, so a re-listing that
    QString m_anchorName;        // reorders files doesn't move it
};

} // namespace ui
