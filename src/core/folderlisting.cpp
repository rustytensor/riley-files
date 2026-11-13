// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "folderlisting.h"

#include "entryorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace Riley {

namespace {

DirEntry entryFromInfo(const QFileInfo &info)
{
    DirEntry entry;
    entry.name = info.fileName();
    entry.path = info.absoluteFilePath();
    entry.isDir = info.isDir();
    entry.size = entry.isDir ? 0 : info.size();
    entry.modified = info.lastModified();
    entry.hidden = entry.name.startsWith(QLatin1Char('.'));
    return entry;
}

} // namespace

ListingResult listFolder(const QString &path, const ListingOptions &options)
{
    return listFolder(path, options, SortKey::Name, {});
}

ListingResult listFolder(const QString &path, const ListingOptions &options, SortKey key,
                         const std::function<bool()> &shouldStop)
{
    ListingResult result;

    const QFileInfo folder(path);
    if (!folder.isDir()) {
        result.error = QCoreApplication::translate("FolderListing", "Not a folder: %1").arg(path);
        return result;
    }
    if (!folder.isReadable()) {
        result.error = QCoreApplication::translate("FolderListing", "No permission to read %1").arg(path);
        return result;
    }

    // System is included so sockets, devices and broken links still show up: they are in
    // the folder, and hiding them would surprise the user.
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (options.showHidden)
        filters |= QDir::Hidden;

    QDirIterator it(path, filters);
    int sinceCheck = 0;
    while (it.hasNext()) {
        result.entries.append(entryFromInfo(it.nextFileInfo()));
        if (shouldStop && ++sinceCheck == 256) {
            sinceCheck = 0;
            if (shouldStop()) {
                result.entries.clear();
                return result;
            }
        }
    }

    if (shouldStop && shouldStop()) {
        result.entries.clear();
        return result;
    }
    EntryOrder(key).sort(result.entries);
    return result;
}

} // namespace Riley
