// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace Riley {

// One thing put in the trash: where it now lives and where it came from.
struct TrashEntry
{
    QString trashedPath;  // .../Trash/files/<name>
    QString infoPath;     // .../Trash/info/<name>.trashinfo
    QString originalPath; // absolute path it had before
    QDateTime deletedAt;  // local time
};

// The home trash of the freedesktop.org trash specification: $XDG_DATA_HOME/Trash (or
// ~/.local/share/Trash), with files/ holding the trashed things and info/ one .trashinfo each.
//
// Only the home trash for now: a file on another drive than the trash cannot be moved in
// without copying, and put() refuses it with a message rather than copying (other drives'
// .Trash-UID folders may come later). It never overwrites anything already in the trash: a
// second "f.txt" goes in as "f (2).txt", each with its own info file.
//
// Safe to call from any thread: it only touches the file system.
class RILEYCORE_EXPORT Trash
{
public:
    // The home trash folder, read from the environment each time.
    static QString homeTrashPath();

    // Moves path (a file, folder or symlink; a symlink itself, never what it points to) into
    // the home trash. On failure returns nothing and says why in error; nothing has moved.
    static std::optional<TrashEntry> put(const QString &path, QString *error = nullptr);

    // What the info file of a thing in the home trash says: its original path and deletion
    // date. Nothing when trashedPath is not in Trash/files or its info file is missing or broken.
    static std::optional<TrashEntry> entryFor(const QString &trashedPath);

    // Puts a thing from the home trash back, at destination, or at the path its info file names
    // when destination is empty. Never overwrites: a taken name is refused. A missing parent
    // folder is made again. The info file is removed once the thing is back. Returns the path
    // it now has; on failure nothing has moved and error says why.
    static std::optional<QString> restore(const QString &trashedPath, const QString &destination = QString(),
                                          QString *error = nullptr);

    // Deletes a thing in the home trash for good, and its info file. Only when the person asks.
    static bool erase(const QString &trashedPath, QString *error = nullptr);

    // Deletes path for good (a folder with everything in it; symlinks themselves, never what
    // they point to). Only when the person asks. Stops at the first thing it cannot remove.
    static bool removeForGood(const QString &path, QString *error = nullptr);
};

} // namespace Riley
