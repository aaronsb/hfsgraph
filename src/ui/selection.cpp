// SPDX-FileCopyrightText: 2026 Aaron Bockelie <aaronsb@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "selection.h"

#include "core/fsnode.h"

#include <algorithm>

namespace ui {

Selection::Selection(QObject *parent) : QObject(parent) {}

int Selection::count() const {
    int n = 0;
    for (const auto &names : m_byDir)
        n += static_cast<int>(names.size());
    return n;
}

bool Selection::contains(const core::MemberKey &dir, const QString &name) const {
    const auto it = m_byDir.constFind(dir);
    return it != m_byDir.constEnd() && it->contains(name);
}

const QSet<QString> *Selection::namesIn(const core::MemberKey &dir) const {
    const auto it = m_byDir.constFind(dir);
    return it == m_byDir.constEnd() ? nullptr : &it.value();
}

void Selection::clear() {
    m_anchorDir.clear();
    m_anchorName.clear();
    if (m_byDir.isEmpty())
        return;
    m_byDir.clear();
    m_dirPath.clear();
    Q_EMIT changed();
}

void Selection::insert(const core::FsNode &dir, int index) {
    if (index < 0 || index >= static_cast<int>(dir.files.size()))
        return;
    const core::MemberKey key = core::keyFor(dir);
    m_byDir[key].insert(dir.files[static_cast<std::size_t>(index)].name);
    // The on-disk location: a projection copy under a staged move carries its
    // destination in `path`; the file is still where it was scanned.
    m_dirPath.insert(key, dir.originalPath.isEmpty() ? dir.path : dir.originalPath);
}

void Selection::setAnchor(const core::FsNode &dir, int index) {
    m_anchorDir = core::keyFor(dir);
    m_anchorName = dir.files[static_cast<std::size_t>(index)].name;
}

void Selection::set(const core::FsNode &dir, int index) {
    if (index < 0 || index >= static_cast<int>(dir.files.size()))
        return;
    const core::MemberKey key = core::keyFor(dir);
    const QString &name = dir.files[static_cast<std::size_t>(index)].name;
    const bool already = m_byDir.size() == 1 && m_byDir.contains(key) && m_byDir[key].size() == 1 &&
                         m_byDir[key].contains(name);
    setAnchor(dir, index);
    if (already)
        return; // exactly this file is selected: nothing to repaint
    m_byDir.clear();
    m_dirPath.clear();
    insert(dir, index);
    Q_EMIT changed();
}

void Selection::toggle(const core::FsNode &dir, int index) {
    if (index < 0 || index >= static_cast<int>(dir.files.size()))
        return;
    const core::MemberKey key = core::keyFor(dir);
    const QString &name = dir.files[static_cast<std::size_t>(index)].name;
    auto it = m_byDir.find(key);
    if (it != m_byDir.end() && it->contains(name)) {
        it->remove(name);
        if (it->isEmpty()) {
            m_byDir.erase(it);
            m_dirPath.remove(key);
        }
    } else {
        insert(dir, index);
    }
    setAnchor(dir, index);
    Q_EMIT changed();
}

void Selection::rangeTo(const core::FsNode &dir, int index) {
    // Ranges are within one directory (file order is the directory's listing order).
    // With no anchor there, or an anchor elsewhere, this is a plain set.
    int anchor = -1;
    if (m_anchorDir == core::keyFor(dir))
        for (std::size_t i = 0; i < dir.files.size(); ++i)
            if (dir.files[i].name == m_anchorName) {
                anchor = static_cast<int>(i);
                break;
            }
    if (anchor < 0 || index < 0 || index >= static_cast<int>(dir.files.size())) {
        set(dir, index);
        return;
    }
    const int lo = std::min(anchor, index), hi = std::max(anchor, index);
    for (int i = lo; i <= hi; ++i)
        insert(dir, i);
    Q_EMIT changed(); // the anchor stays where it was
}

void Selection::add(const core::FsNode &dir, int index) {
    if (contains(core::keyFor(dir), dir.files[static_cast<std::size_t>(index)].name))
        return;
    insert(dir, index);
    Q_EMIT changed();
}

void Selection::addAll(const core::FsNode &dir, const std::vector<int> &indices) {
    const core::MemberKey key = core::keyFor(dir);
    bool grew = false;
    for (int i : indices) {
        if (i < 0 || i >= static_cast<int>(dir.files.size()))
            continue;
        if (!contains(key, dir.files[static_cast<std::size_t>(i)].name)) {
            insert(dir, i);
            grew = true;
        }
    }
    if (grew)
        Q_EMIT changed();
}

QList<QUrl> Selection::urls() const {
    QList<QUrl> out;
    for (auto it = m_byDir.constBegin(); it != m_byDir.constEnd(); ++it) {
        const QString base = m_dirPath.value(it.key());
        for (const QString &name : it.value())
            out << QUrl::fromLocalFile(base + QLatin1Char('/') + name);
    }
    return out;
}

} // namespace ui
