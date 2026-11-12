// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "entryorder.h"

#include <algorithm>

namespace Riley {

EntryOrder::EntryOrder(const QLocale &locale)
    : EntryOrder(SortKey::Name, locale)
{
}

EntryOrder::EntryOrder(SortKey key, const QLocale &locale)
    : m_collator(locale)
    , m_key(key)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);
}

QStringView EntryOrder::extensionOf(QStringView name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return {};
    return name.mid(dot + 1);
}

bool EntryOrder::lessThan(const DirEntry &a, const DirEntry &b) const
{
    if (a.isDir != b.isDir)
        return a.isDir;

    switch (m_key) {
    case SortKey::Name:
        break;
    case SortKey::Size:
        if (a.size != b.size)
            return a.size > b.size;
        break;
    case SortKey::Modified:
        if (a.modified != b.modified)
            return a.modified > b.modified;
        break;
    case SortKey::Kind:
        if (!a.isDir) {
            if (const int byKind = m_collator.compare(extensionOf(a.name), extensionOf(b.name));
                byKind != 0)
                return byKind < 0;
        }
        break;
    }
    return nameLess(a, b);
}

bool EntryOrder::nameLess(const DirEntry &a, const DirEntry &b) const
{
    if (const int byName = m_collator.compare(a.name, b.name); byName != 0)
        return byName < 0;

    // Names that only differ in case (or in ways the collator ignores) still need a fixed
    // order, or the list would shuffle between reloads.
    return a.name < b.name;
}

void EntryOrder::sort(QList<DirEntry> &entries) const
{
    std::sort(entries.begin(), entries.end(),
              [this](const DirEntry &a, const DirEntry &b) { return lessThan(a, b); });
}

} // namespace Riley
