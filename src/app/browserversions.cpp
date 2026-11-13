// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Previous versions from the snapshots above a file (the snapper layout): the list of them in
// the folder's place, restoring one (the live file goes to the trash first, so it is never lost),
// restoring one as a copy beside it, looking through a folder's version read-only, and refusing
// every change inside a snapshot.

#include "browser.h"

#include <QFileInfo>

namespace {

const QString versionsPlace = QStringLiteral("versions");

} // namespace

void Browser::showVersions()
{
    if (somethingInFront() || !m_place.isEmpty())
        return;
    const QString path = m_versions->path();
    if (path.isEmpty() || !m_versions->isAvailable()) {
        m_ops->say(tr("There are no snapshots of “%1”.").arg(QFileInfo(path).fileName()));
        return;
    }
    closeSearch();
    m_ops->cancelRename();
    m_preview->setOpen(false);
    m_versionsOf = path;
    setPlace(versionsPlace);
    followPlaceRows();
}

void Browser::openVersionRow(int row)
{
    const QList<Riley::FileVersion> list = m_versions->versions();
    if (row < 0 || row >= list.size())
        return;
    setPlaceRow(row);
    const Riley::FileVersion version = list.at(row);
    if (version.isFolder) {
        // A folder's version is looked through like any folder; nothing in it can be changed.
        open(version.path);
        return;
    }
    restoreVersion(row, false);
}

void Browser::restoreVersion(int row, bool asCopy)
{
    if (m_place != versionsPlace)
        return;
    const QList<Riley::FileVersion> list = m_versions->versions();
    if (row < 0 || row >= list.size()) {
        m_ops->say(tr("Choose a version to restore."));
        return;
    }
    const Riley::FileVersion version = list.at(row);
    const QString live = m_versionsOf;
    setPlaceRow(row);
    // Once done, the folder is shown again with the keys on what was restored.
    const auto restore = [this, version, live, asCopy] {
        leavePlace();
        m_ops->restoreVersion(version.path, live, version.snapshot.date, asCopy);
    };
    if (asCopy)
        restore();
    else
        ask(tr("Restore this version?"), restore);
}

QList<Actions::Entry> Browser::versionMenu() const
{
    using Entry = Actions::Entry;
    return {Entry::action(QStringLiteral("restore-version")), Entry::action(QStringLiteral("restore-version-copy")),
            Entry::separator(), Entry::action(QStringLiteral("open-version"))};
}

void Browser::versionMenuAt(int row, qreal x, qreal y)
{
    if (m_place != versionsPlace || somethingInFront())
        return;
    if (row >= 0)
        setPlaceRow(row);
    m_actions->openMenu(versionMenu(), QPointF(x, y));
}

bool Browser::inSnapshot() const
{
    return m_place.isEmpty() && Riley::Snapshots::isInSnapshot(workFolder());
}

bool Browser::refusedInSnapshot(const QString &actionId)
{
    static const QStringList changes = {
        QStringLiteral("rename"), QStringLiteral("trash"), QStringLiteral("delete-for-good"),
        QStringLiteral("new-folder"), QStringLiteral("paste"), QStringLiteral("cut"),
    };
    if (!changes.contains(actionId) || !inSnapshot())
        return false;
    m_ops->say(Riley::Snapshots::readOnlyMessage());
    return true;
}
