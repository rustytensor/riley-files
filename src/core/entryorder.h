// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "direntry.h"
#include "rileycore_export.h"

#include <QCollator>
#include <QList>
#include <QLocale>

namespace Riley {

// The order a person expects: folders before files, then names compared the way they read,
// ignoring case and taking runs of digits by value ("File2" before "file10").
class RILEYCORE_EXPORT EntryOrder
{
public:
    explicit EntryOrder(const QLocale &locale = QLocale());

    // True when a belongs before b.
    bool lessThan(const DirEntry &a, const DirEntry &b) const;

    // Sorts the entries in place.
    void sort(QList<DirEntry> &entries) const;

private:
    QCollator m_collator;
};

} // namespace Riley
