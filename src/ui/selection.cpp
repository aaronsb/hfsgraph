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
    m_dirPath.insert(key, dir.path);
}

void Selection::setAnchor(const core::FsNode &dir, int index) {
    m_anchorDir = core::keyFor(dir);
    m_anchorIndex = index;
}

void Selection::set(const core::FsNode &dir, int index) {
    m_byDir.clear();
    m_dirPath.clear();
    insert(dir, index);
    setAnchor(dir, index);
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
    if (m_anchorIndex < 0 || m_anchorDir != core::keyFor(dir)) {
        set(dir, index);
        return;
    }
    const int lo = std::min(m_anchorIndex, index), hi = std::max(m_anchorIndex, index);
    for (int i = lo; i <= hi; ++i)
        insert(dir, i);
    Q_EMIT changed(); // the anchor stays where it was
}

void Selection::add(const core::FsNode &dir, int index) {
    insert(dir, index);
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
