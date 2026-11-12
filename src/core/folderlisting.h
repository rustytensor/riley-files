// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "direntry.h"
#include "entryorder.h"
#include "rileycore_export.h"

#include <QList>
#include <QString>

#include <functional>

namespace Riley {

struct ListingOptions
{
    bool showHidden = false; // include names that start with a dot
};

struct ListingResult
{
    QList<DirEntry> entries; // in EntryOrder
    QString error;           // empty when the folder was read

    bool ok() const { return error.isEmpty(); }
};

// Reads one folder, sorted folders first and then by name the way a person reads it.
// This blocks while it reads; callers that must stay responsive run it off their own thread.
RILEYCORE_EXPORT ListingResult listFolder(const QString &path, const ListingOptions &options = {});

// The same, sorted by the given key, giving up early (with an empty result) as soon as
// shouldStop returns true. shouldStop is asked every few hundred entries, from the calling thread.
RILEYCORE_EXPORT ListingResult listFolder(const QString &path, const ListingOptions &options,
                                          SortKey key, const std::function<bool()> &shouldStop);

} // namespace Riley
