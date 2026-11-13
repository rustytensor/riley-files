// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "direntry.h"
#include "rileycore_export.h"

#include <QCollator>
#include <QList>
#include <QLocale>

namespace Riley {

// What a folder is sorted by. Folders come before files unless that is turned off; ties fall back
// to the name.
enum class SortKey {
    Name,     // names the way a person reads them
    Size,     // largest first
    Modified, // newest first
    Kind,     // by extension, then by name
};

// The order a person expects: folders before files, then names compared the way they read,
// ignoring case and taking runs of digits by value ("File2" before "file10"). Names that only
// differ in case are ordered by plain comparison, so the order never shuffles between reloads.
// Other sort keys come first when chosen, and the name order settles ties.
//
// Not thread safe: each thread makes its own.
class RILEYCORE_EXPORT EntryOrder
{
public:
    explicit EntryOrder(const QLocale &locale = QLocale());
    explicit EntryOrder(SortKey key, const QLocale &locale = QLocale());

    SortKey key() const { return m_key; }

    // Folders before files (the default), or folders and files mixed in the one order.
    bool foldersFirst() const { return m_foldersFirst; }
    void setFoldersFirst(bool on) { m_foldersFirst = on; }

    // True when a belongs before b.
    bool lessThan(const DirEntry &a, const DirEntry &b) const;

    // Sorts the entries in place.
    void sort(QList<DirEntry> &entries) const;

    // The extension used by the kind order: what follows the last dot, but not for names that
    // only start with one (".bashrc" has none).
    static QStringView extensionOf(QStringView name);

private:
    bool nameLess(const DirEntry &a, const DirEntry &b) const;

    QCollator m_collator;
    SortKey m_key = SortKey::Name;
    bool m_foldersFirst = true;
};

} // namespace Riley
