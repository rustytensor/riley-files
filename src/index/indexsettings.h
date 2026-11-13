// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileyindex_export.h"

#include <QString>
#include <QStringList>

class QSettings;

namespace Riley {

// What the person chose to index, from Preferences > Index. Kept in the program's settings file
// (the [index] group), which the window writes and the indexer service reads.
struct RILEYINDEX_EXPORT IndexSettings
{
    // Indexing on at all. Off: the service is not started, and search walks the folders.
    bool enabled = true;
    // The folders indexed, with everything below them.
    QStringList folders;
    // Names left out, with everything below them. A pattern is matched against the name
    // ("*.log", "node_modules"); one holding a "/" against the whole path; one starting with
    // "/" leaves out that folder.
    QStringList excludes;
    // The folders whose files' contents are indexed too (each at or below one of `folders`).
    QStringList contentFolders;
    // Files larger than this are indexed by name only.
    qint64 contentLimit = 1024 * 1024;
    // Only text files (UTF-8, no zero bytes) have their contents indexed.
    bool textOnly = true;

    bool operator==(const IndexSettings &other) const = default;

    // Home, the default exclude patterns, contents nowhere, 1 MB, text only.
    static IndexSettings defaults();
    static QStringList defaultExcludes();
    // The program's settings file (the window's Preferences are kept there).
    static QString defaultFile();

    // From the [index] group; what it does not hold comes from defaults().
    static IndexSettings load(QSettings &settings);
    void save(QSettings &settings) const;
    static IndexSettings loadFile(const QString &file);
    bool saveFile(const QString &file) const;

    // The folders to index, cleaned and absolute, sorted, without repeats.
    QStringList roots() const;
    // Whether an entry with this name and absolute path is left out.
    bool isExcluded(const QString &name, const QString &path) const;
    // Whether files at this absolute path have their contents indexed.
    bool contentsWanted(const QString &path) const;
};

// Whether `path` is `folder` or below it (both cleaned and absolute).
RILEYINDEX_EXPORT bool isAtOrBelow(const QString &path, const QString &folder);

} // namespace Riley
