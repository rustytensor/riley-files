// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The places that are not folders: the trash view (everything in the home trash and in each
// drive's own, with Restore, Delete for good and Empty Trash) and the recent files; deleting
// for good (Shift+Delete), always after asking; and Connect to Server.

#include "browser.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {

const QString trashPlace = QStringLiteral("trash");
const QString recentPlace = QStringLiteral("recent");
const QString versionsPlace = QStringLiteral("versions");

} // namespace

QString Browser::trashPlacePath()
{
    return QDir::cleanPath(Riley::Trash::homeTrashPath() + QStringLiteral("/files"));
}

QStringList Browser::trashFolders() const
{
    QStringList tops;
    for (const Riley::Mount &drive : m_drives->drives())
        tops.append(drive.mountPoint);
    return Riley::Trash::trashFolders(tops);
}

void Browser::setPlace(const QString &place)
{
    if (place == m_place)
        return;
    const bool leftVersions = m_place == versionsPlace;
    m_place = place;
    m_placeRow = -1;
    m_placeRowPath.clear();
    if (leftVersions) {
        // The versions follow the keys again.
        m_versionsOf.clear();
        m_versions->setPath(focusPath());
    }
    emit placeChanged();
    emit placeRowChanged();
    emit folderChanged(); // the title is the place's
    emit sidebarChanged();
}

void Browser::leavePlace()
{
    setPlace(QString());
}

void Browser::showTrash()
{
    closeSearch();
    m_ops->cancelRename();
    m_preview->setOpen(false);
    reloadTrash();
    setPlace(trashPlace);
    followPlaceRows();
}

void Browser::showRecent()
{
    closeSearch();
    m_ops->cancelRename();
    m_preview->setOpen(false);
    m_recent->reload();
    setPlace(recentPlace);
    followPlaceRows();
}

void Browser::reloadTrash()
{
    m_trash->reload(trashFolders());
}

int Browser::placeCount() const
{
    if (m_place == trashPlace)
        return m_trash->count();
    if (m_place == recentPlace)
        return m_recent->count();
    if (m_place == versionsPlace)
        return m_versions->count();
    return 0;
}

QString Browser::placePathAt(int row) const
{
    if (m_place == trashPlace)
        return m_trash->pathAt(row);
    if (m_place == recentPlace)
        return m_recent->pathAt(row);
    if (m_place == versionsPlace)
        return m_versions->copyPathAt(row);
    return {};
}

void Browser::setPlaceRow(int row)
{
    const int n = placeCount();
    row = n == 0 ? -1 : std::clamp(row, 0, n - 1);
    const QString path = placePathAt(row);
    if (row == m_placeRow && path == m_placeRowPath)
        return;
    m_placeRow = row;
    m_placeRowPath = path;
    emit placeRowChanged();
}

// After the place's rows changed: the keys stay on their thing, or about where they were.
void Browser::followPlaceRows()
{
    if (m_place.isEmpty())
        return;
    int row = -1;
    if (!m_placeRowPath.isEmpty()) {
        if (m_place == trashPlace)
            row = m_trash->rowOf(m_placeRowPath);
        else if (m_place == recentPlace)
            row = m_recent->rowOf(m_placeRowPath);
        else
            for (int i = 0; i < m_versions->count() && row < 0; ++i)
                if (m_versions->copyPathAt(i) == m_placeRowPath)
                    row = i;
    }
    if (row < 0)
        row = std::max(m_placeRow, 0);
    // Set again even when the row number is the same: the thing on it may be another.
    m_placeRow = -2;
    setPlaceRow(row);
}

void Browser::clickPlaceRow(int row)
{
    if (somethingInFront())
        return;
    setSidebarFocus(false);
    setPlaceRow(row);
}

void Browser::openPlaceRow(int row)
{
    if (somethingInFront())
        return;
    setPlaceRow(row);
    if (m_place == trashPlace) {
        restoreRow(row);
        return;
    }
    if (m_place == versionsPlace) {
        openVersionRow(row);
        return;
    }
    const QString path = placePathAt(row);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).isDir())
        open(path);
    else
        openFiles({path});
}

void Browser::restoreRow(int row)
{
    if (m_place != trashPlace)
        return;
    const QString path = m_trash->pathAt(row);
    if (path.isEmpty()) {
        m_ops->say(tr("Choose something in the trash to restore."));
        return;
    }
    m_ops->restore({path});
}

void Browser::deleteRowForGood(int row)
{
    if (m_place != trashPlace)
        return;
    const QString path = m_trash->pathAt(row);
    if (path.isEmpty())
        return;
    askDeleteForGood({path}, m_trash->nameAt(row));
}

void Browser::emptyTrash()
{
    reloadTrash();
    const QStringList paths = m_trash->paths();
    if (paths.isEmpty()) {
        m_ops->say(tr("The trash is empty."));
        return;
    }
    ask(paths.size() == 1
            ? tr("Empty the trash? The 1 item in it will be deleted for good; this cannot be undone.")
            : tr("Empty the trash? The %n items in it will be deleted for good; this cannot be undone.", nullptr,
                 int(paths.size())),
        [this, paths] { m_ops->deleteForGood(paths); });
}

void Browser::deleteTargetsForGood()
{
    if (m_place == trashPlace) {
        deleteRowForGood(m_placeRow);
        return;
    }
    if (!m_place.isEmpty())
        return;
    const QStringList paths = targetPaths();
    if (paths.isEmpty())
        return;
    askDeleteForGood(paths, QFileInfo(paths.first()).fileName());
}

void Browser::askDeleteForGood(const QStringList &paths, const QString &name)
{
    const QString text = paths.size() == 1
                             ? tr("Delete “%1” for good? This cannot be undone.").arg(name)
                             : tr("Delete %n items for good? This cannot be undone.", nullptr, int(paths.size()));
    ask(text, [this, paths] { m_ops->deleteForGood(paths); });
}

bool Browser::placeKey(int key, Qt::KeyboardModifiers mods)
{
    if (key == Qt::Key_Delete && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        // In the trash, deleting is for good, and asked first. The recent files are only
        // bookmarks: Delete does nothing there.
        if (m_place == trashPlace)
            deleteRowForGood(m_placeRow);
        return true;
    }
    if (m_place == versionsPlace
        && ((key == Qt::Key_Menu && mods == Qt::NoModifier) || (key == Qt::Key_F10 && mods == Qt::ShiftModifier))) {
        versionMenuAt(m_placeRow, -1, -1);
        return true;
    }
    if (mods != Qt::NoModifier)
        return false;
    const int n = placeCount();
    switch (key) {
    case Qt::Key_Up:
        if (n > 0)
            setPlaceRow(m_placeRow < 0 ? 0 : m_placeRow - 1);
        return true;
    case Qt::Key_Down:
        if (n > 0)
            setPlaceRow(m_placeRow + 1);
        return true;
    case Qt::Key_Home:
        setPlaceRow(0);
        return true;
    case Qt::Key_End:
        setPlaceRow(n - 1);
        return true;
    case Qt::Key_PageUp:
        setPlaceRow(m_placeRow - m_pageRows);
        return true;
    case Qt::Key_PageDown:
        setPlaceRow(m_placeRow + m_pageRows);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        openPlaceRow(m_placeRow);
        return true;
    case Qt::Key_Escape:
        if (!m_ops->message().isEmpty())
            m_ops->dismissMessage();
        else
            leavePlace();
        return true;
    default:
        return false;
    }
}

bool Browser::placeAllows(const QString &actionId) const
{
    // What makes sense with the folder hidden behind the place.
    static const QStringList allowed = {
        QStringLiteral("find"),         QStringLiteral("find-contents"), QStringLiteral("sidebar"),
        QStringLiteral("details"),      QStringLiteral("preferences"),   QStringLiteral("shortcuts"),
        QStringLiteral("about"),        QStringLiteral("new-window"),    QStringLiteral("close-window"),
        QStringLiteral("quit"),         QStringLiteral("back"),          QStringLiteral("home"),
        QStringLiteral("location"),     QStringLiteral("reload"),        QStringLiteral("restore"),
        QStringLiteral("delete-for-good"), QStringLiteral("empty-trash"), QStringLiteral("connect"),
        QStringLiteral("recent"),       QStringLiteral("undo"),          QStringLiteral("redo"),
        QStringLiteral("restore-version"), QStringLiteral("restore-version-copy"),
    };
    return allowed.contains(actionId);
}

void Browser::openConnect()
{
    if (m_connectShown)
        return;
    m_connectShown = true;
    emit connectShownChanged();
}

void Browser::cancelConnect()
{
    if (!m_connectShown)
        return;
    m_connectShown = false;
    emit connectShownChanged();
}

void Browser::connectToServer(const QString &uri)
{
    cancelConnect();
    const QString address = uri.trimmed();
    if (address.isEmpty())
        return;
    QString why;
    if (!m_network->connectTo(address, &why)) {
        m_ops->say(why);
        return;
    }
    m_ops->say(tr("Connecting to %1…").arg(address));
}
