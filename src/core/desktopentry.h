// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Riley {

// An application, as its .desktop file describes it (the freedesktop Desktop Entry
// specification): its id, name, command and the MIME types it opens.
struct RILEYCORE_EXPORT DesktopEntry
{
    QString id;   // "org.example.Editor.desktop": the path below applications/, '/' made '-'
    QString file; // the .desktop file itself, absolute
    QString name;
    QString exec; // the Exec value, string escapes already undone
    QString icon;
    QString tryExec;
    QStringList mimeTypes;
    bool noDisplay = false;
    bool hidden = false;
    bool application = false; // Type=Application

    bool isValid() const { return application && !hidden && !exec.isEmpty(); }

    // Reads the [Desktop Entry] group of `file`, whose id is `id`. An entry that cannot be read
    // comes back invalid.
    static DesktopEntry read(const QString &file, const QString &id);

    // The Exec value split into arguments by the specification's quoting rules (double quotes,
    // and inside them \" \` \$ \\). False when the quoting is broken.
    static bool splitExec(const QString &exec, QStringList *args);

    // The command lines that open `paths` (absolute local files) with this application, its
    // field codes filled in: %f and %u take one file each, so they make one command line per
    // file; %F and %U take them all at once; %i, %c and %k give the icon, the name and the
    // .desktop file; %% is a percent sign; the deprecated codes are dropped. Empty when the
    // Exec value is broken. The first argument is the program, never a shell.
    QList<QStringList> commandLines(const QStringList &paths) const;
};

} // namespace Riley
