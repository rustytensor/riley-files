// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "desktopentry.h"
#include "rileycore_export.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Riley {

// Which applications open which kinds of file, per the freedesktop "Association between MIME
// types and applications" specification, read fresh from the files on every call.
//
// The applications are the .desktop files under applications/ in $XDG_DATA_HOME and then each
// $XDG_DATA_DIRS folder (an id found earlier hides the same id later). The choices are the
// mimeapps.list files, most important first: $XDG_CONFIG_HOME, $XDG_CONFIG_DIRS,
// $XDG_DATA_HOME/applications, $XDG_DATA_DIRS/applications, each with the desktop's own
// "<desktop>-mimeapps.list" before the plain one.
//
// A default chosen here is written to $XDG_CONFIG_HOME/mimeapps.list, under
// [Default Applications]; every other line of that file stays as it was.
class RILEYCORE_EXPORT MimeApps
{
public:
    // The mimeapps.list files, most important first (also those that do not exist).
    static QStringList listFiles();
    // The applications/ folders, most important first.
    static QStringList applicationDirs();
    // The file a default is written to: $XDG_CONFIG_HOME/mimeapps.list.
    static QString userListFile();

    // Every installed application (valid, not hidden, its TryExec found), by name.
    static QList<DesktopEntry> allApplications();
    // The installed application with this id, or an invalid entry.
    static DesktopEntry application(const QString &id);

    // The applications for `mimeType`, the default first; then those for the types it is a
    // kind of (text/plain for a C source file), after the type's own.
    static QList<DesktopEntry> applicationsFor(const QString &mimeType);
    // The id of the default application for `mimeType`, or empty when none is installed.
    static QString defaultFor(const QString &mimeType);

    // Makes `desktopId` the default for `mimeType`. False when the file could not be written.
    static bool setDefault(const QString &mimeType, const QString &desktopId);

    // Starts `app` on `paths` (absolute local files), never through a shell. False, with
    // `error` saying why, when it could not be started.
    static bool launch(const DesktopEntry &app, const QStringList &paths, QString *error = nullptr);
};

} // namespace Riley
