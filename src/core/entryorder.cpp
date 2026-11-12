// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "entryorder.h"

#include <algorithm>

namespace Riley {

EntryOrder::EntryOrder(const QLocale &locale)
    : m_collator(locale)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);
}

bool EntryOrder::lessThan(const DirEntry &a, const DirEntry &b) const
{
    if (a.isDir != b.isDir)
        return a.isDir;

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
