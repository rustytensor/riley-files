// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace Riley {

// One thing put in the trash: where it now lives and where it came from.
struct TrashEntry
{
    QString trashedPath;  // <trash>/files/<name>
    QString infoPath;     // <trash>/info/<name>.trashinfo
    QString originalPath; // absolute path it had before
    QDateTime deletedAt;  // local time
};

// The trash of the freedesktop.org trash specification. Each trash folder has files/ holding
// the trashed things and info/ one .trashinfo each.
//
// A thing on the same file system as the home trash ($XDG_DATA_HOME/Trash, or
// ~/.local/share/Trash) goes there, its info file naming its absolute path. A thing on another
// file system (a USB stick, another disk) goes to that file system's own trash,
// <top>/.Trash-<uid>, where <top> is the folder the file system is mounted on; its info file
// names its path relative to <top>, so the drive can be mounted elsewhere later. Nothing is
// ever copied across drives. It never overwrites anything already in a trash: a second "f.txt"
// goes in as "f (2).txt", each with its own info file.
//
// Safe to call from any thread: it only touches the file system.
class RILEYCORE_EXPORT Trash
{
public:
    // The home trash folder, read from the environment each time.
    static QString homeTrashPath();

    // The folder the file system holding path is mounted on (path itself when it is a mount
    // point; a symlink counts where it is, not where it points). Empty when path is not there.
    static QString topDirOf(const QString &path);
    // The trash folder of the file system mounted on topDir: topDir/.Trash-<uid>.
    static QString driveTrashPath(const QString &topDir);
    // The trash folder path would go to: the home trash, or its drive's .Trash-<uid> (which may
    // not exist yet). Empty, with error saying why, when it cannot go to any.
    static QString trashPathFor(const QString &path, QString *error = nullptr);
    // True when path is directly in the files/ folder of the home trash or of a drive's trash.
    static bool isInTrash(const QString &path);
    // Every trash folder there is: the home trash, then the .Trash-<uid> of each of the given
    // drives' top folders that exists and is the person's own. For the trash view.
    static QStringList trashFolders(const QStringList &driveTops);

    // Moves path (a file, folder or symlink; a symlink itself, never what it points to) into
    // its trash (see above). On failure returns nothing and says why in error; nothing has moved.
    static std::optional<TrashEntry> put(const QString &path, QString *error = nullptr);

    // What the info file of a thing in a trash says: its original path (absolute, a drive's
    // relative paths made whole) and deletion date. Nothing when trashedPath is not in a
    // trash's files/ or its info file is missing or broken.
    static std::optional<TrashEntry> entryFor(const QString &trashedPath);

    // Puts a thing from a trash back, at destination, or at the path its info file names
    // when destination is empty. Never overwrites: a taken name is refused. A missing parent
    // folder is made again. The info file is removed once the thing is back. Returns the path
    // it now has; on failure nothing has moved and error says why.
    static std::optional<QString> restore(const QString &trashedPath, const QString &destination = QString(),
                                          QString *error = nullptr);

    // Deletes a thing in a trash for good, and its info file. Only when the person asks.
    static bool erase(const QString &trashedPath, QString *error = nullptr);

    // Deletes path for good (a folder with everything in it; symlinks themselves, never what
    // they point to). Only when the person asks. Stops at the first thing it cannot remove.
    static bool removeForGood(const QString &path, QString *error = nullptr);
};

} // namespace Riley
