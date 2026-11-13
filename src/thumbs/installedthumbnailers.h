// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileythumbs_export.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Riley {

// The thumbnail makers other programs install: <data dir>/thumbnailers/*.thumbnailer, read from
// XDG_DATA_HOME first and then each XDG_DATA_DIRS folder. A file name found earlier hides the
// same name later, so a user can replace one. Each file says, in its [Thumbnailer Entry]:
//
//     TryExec=program            (optional: skipped when the program is not installed)
//     Exec=program %i %o %s      (%i input path, %u input URI, %o output PNG, %s size, %% a %)
//     MimeType=type/one;type/two;
class RILEYTHUMBS_EXPORT InstalledThumbnailers
{
public:
    struct Entry {
        QString file;       // the .thumbnailer file
        QString exec;       // its Exec line, unexpanded
        QStringList mimeTypes;
    };

    // Reads the folders now.
    InstalledThumbnailers();

    const QList<Entry> &entries() const { return m_entries; }

    // The thumbnailer for exactly this MIME type, or null.
    const Entry *forMimeType(const QString &mimeType) const;

    // The program and its arguments for one run of `exec`: the line is split as a desktop entry's
    // Exec is (spaces separate, double quotes group, backslash escapes inside quotes), and the
    // field codes are replaced. Empty when the line is empty or badly quoted.
    static QStringList command(const QString &exec, const QString &input, const QString &output,
                               int size);

private:
    QList<Entry> m_entries;
    QHash<QString, int> m_byMime;
};

} // namespace Riley
