// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fileactions.h"

#include "core/fsnode.h"
#include "graphscene.h"
#include "selection.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QMenu>
#include <QUrl>

#include <KFileItem>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KPropertiesDialog>

namespace ui {

FileActions::FileActions(GraphScene *scene, QObject *parent) : QObject(parent), m_scene(scene) {}

void FileActions::populate(QMenu &menu, QWidget *parent) {
    const Selection &sel = m_scene->selection();
    const int n = sel.count();
    if (n == 0)
        return;
    const QList<QUrl> urls = sel.urls();

    menu.addAction(QStringLiteral("Open"), this, [this, parent] { openSelected(parent); });

    // "Open With…" — the same applications the file manager offers, from KIO.
    KFileItemList items;
    for (const QUrl &u : urls)
        items << KFileItem(u);
    auto *kfa = new KFileItemActions(&menu);
    kfa->setParentWidget(parent);
    kfa->setItemListProperties(KFileItemListProperties(items));
    QAction *sep = menu.addSeparator();
    kfa->insertOpenWithActionsTo(sep, &menu, QStringList());

    menu.addAction(QStringLiteral("Reveal in file manager"), this, &FileActions::revealSelected);
    menu.addAction(n == 1 ? QStringLiteral("Copy path") : QStringLiteral("Copy %1 paths").arg(n),
                   this, &FileActions::copyPaths);
    menu.addAction(QStringLiteral("Properties…"), this, [this, parent] { showProperties(parent); });

    menu.addSeparator();
    // Staged: previewed in the map, applied with the plan.
    QAction *rename =
        menu.addAction(QStringLiteral("Rename…"), this, [this, parent] { renameSelected(parent); });
    rename->setEnabled(n == 1);
    menu.addAction(n == 1 ? QStringLiteral("Move to Trash")
                          : QStringLiteral("Move %1 files to Trash").arg(n),
                   this, &FileActions::trashSelected);
}

void FileActions::populateDir(QMenu &menu, const core::FsNode *dir, QWidget *parent) {
    if (!dir)
        return;
    const QString path = dir->originalPath.isEmpty() ? dir->path : dir->originalPath;
    menu.addAction(QStringLiteral("Open in file manager"), this, [path, parent] {
        auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(path));
        job->setUiDelegate(
            KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, parent));
        job->start();
    });
    menu.addAction(QStringLiteral("Copy path"), this,
                   [path] { QApplication::clipboard()->setText(path); });
}

void FileActions::openSelected(QWidget *parent) {
    for (const QUrl &u : m_scene->selection().urls()) {
        auto *job = new KIO::OpenUrlJob(u);
        job->setUiDelegate(
            KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, parent));
        job->start();
    }
}

void FileActions::revealSelected() {
    KIO::highlightInFileManager(m_scene->selection().urls());
}

void FileActions::copyPaths() {
    QStringList paths;
    for (const QUrl &u : m_scene->selection().urls())
        paths << u.toLocalFile();
    QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
}

void FileActions::showProperties(QWidget *parent) {
    const QList<QUrl> urls = m_scene->selection().urls();
    if (urls.isEmpty())
        return;
    KPropertiesDialog::showDialog(urls.size() == 1 ? urls.first() : urls.first(), parent);
}

void FileActions::renameSelected(QWidget *parent) {
    const std::vector<Selection::Entry> entries = m_scene->selection().entries();
    if (entries.size() != 1)
        return;
    const Selection::Entry &e = entries.front();
    bool ok = false;
    const QString newName = QInputDialog::getText(parent, QStringLiteral("Rename"),
                                                  QStringLiteral("New name for %1:").arg(e.name),
                                                  QLineEdit::Normal, e.name, &ok);
    if (!ok || newName.isEmpty() || newName == e.name)
        return;
    m_scene->stageRename(e.dir, e.name, newName);
}

void FileActions::trashSelected() {
    for (const Selection::Entry &e : m_scene->selection().entries())
        m_scene->stageTrash(e.dir, e.name);
}

} // namespace ui
