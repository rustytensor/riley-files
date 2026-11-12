// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace Riley {

// One item in a folder, as the rest of the program sees it. A plain value: cheap to copy,
// safe to hand between threads, and it never touches the disk after it is made.
struct DirEntry
{
    QString name;        // the file name alone, as shown to the user
    QString path;        // the absolute path
    bool isDir = false;  // true for folders, and for links that point to a folder
    qint64 size = 0;     // bytes; 0 for folders
    QDateTime modified;  // last modification time
    bool hidden = false; // the name starts with a dot

    friend bool operator==(const DirEntry &, const DirEntry &) = default;
};

} // namespace Riley
