// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "filejob.h"
#include "rileycore_export.h"

#include <QMutex>
#include <QString>
#include <QStringList>

namespace Riley {

// Sends files and folders to the home trash (see Trash). What cannot go there is a failure
// and stays where it is; nothing is deleted instead. Nothing in a snapshot is trashed. Each item is one Trashed step.
class RILEYCORE_EXPORT TrashJob : public FileJob
{
    Q_OBJECT

public:
    explicit TrashJob(const QStringList &paths, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Trash; }

    // Where each item now lives in Trash/files, in the order of the paths; complete once done.
    QStringList trashedPaths() const;

protected:
    void run() override;

private:
    const QStringList m_paths;
    mutable QMutex m_resultMutex;
    QStringList m_trashed;
};

// Puts things from a trash (paths in a trash's files/) back where they came from. Nothing is
// ever overwritten: a taken name is a failure, or, with askOnClash, a question (Skip, Keep
// both: the thing comes back under a free name, Replace: the one in the way goes to the trash
// first). Each item is one Restored step.
class RILEYCORE_EXPORT RestoreJob : public FileJob
{
    Q_OBJECT

public:
    explicit RestoreJob(const QStringList &trashedPaths, QObject *parent = nullptr);
    RestoreJob(const QStringList &trashedPaths, bool askOnClash, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Restore; }

    // Where each item is now, in the order given; complete once done.
    QStringList restoredPaths() const;

protected:
    void run() override;

private:
    const QStringList m_paths;
    const bool m_ask = false;
    mutable QMutex m_resultMutex;
    QStringList m_restored;
};

// Deletes for good: only when the person asks for it (emptying the trash, or a delete that
// skips the trash). Items in Trash/files lose their info file too. It cannot be undone, so it
// records no steps. Nothing in a snapshot is deleted.
class RILEYCORE_EXPORT DeleteJob : public FileJob
{
    Q_OBJECT

public:
    explicit DeleteJob(const QStringList &paths, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Delete; }

protected:
    void run() override;

private:
    const QStringList m_paths;
};

} // namespace Riley
