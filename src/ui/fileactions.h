// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Actions on the file selection (#38, the file-ops prototype), in two classes:
//   * immediate — open, open with…, reveal in the file manager, copy path,
//     properties: read-only from hfsgraph's point of view, handed to KIO and done;
//   * staged — rename, move to trash (and move-to-directory via drop): a MoveOp on
//     the ledger, previewed in the projection and verified/committed with the rest
//     of the plan (ADR-200/302). Nothing here writes to disk.
// Builds the context menu for a file glyph or the selection; owned by the scene.
#pragma once

#include <QObject>
#include <QString>

class QMenu;
class QWidget;

namespace core {
struct FsNode;
}

namespace ui {

class GraphScene;

class FileActions : public QObject {
    Q_OBJECT
  public:
    explicit FileActions(GraphScene *scene, QObject *parent = nullptr);

    // Fill `menu` for the current selection (non-empty). `parent` hosts dialogs.
    void populate(QMenu &menu, QWidget *parent);
    // Fill `menu` for a directory cell: open in the file manager, copy path.
    void populateDir(QMenu &menu, const core::FsNode *dir, QWidget *parent);

  public Q_SLOTS:
    void openSelected(QWidget *parent);
    void revealSelected();
    void copyPaths();
    void showProperties(QWidget *parent);
    void renameSelected(QWidget *parent); // single file: asks for the new name, stages it
    void trashSelected();                 // stages a trash op per selected file

  private:
    GraphScene *m_scene;
};

} // namespace ui
