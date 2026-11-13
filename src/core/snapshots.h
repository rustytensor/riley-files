// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>

namespace Riley {

// One snapshot in the snapper layout: <base>/.snapshots/<number>/info.xml says when it was
// taken and why, and <base>/.snapshots/<number>/snapshot/ holds the files as they were.
struct Snapshot
{
    int number = 0;
    QDateTime date;      // local time
    QString description; // as snapper wrote it; may be empty
    QString folder;      // <base>/.snapshots/<number>/snapshot
};

// One earlier version of a file or folder: its copy in a snapshot. Snapshots in a row holding
// the same copy of a file make one version, dated by the oldest of them; snapshot is that one
// and numbers lists them all, oldest first.
struct FileVersion
{
    Snapshot snapshot;
    QList<int> numbers;
    QString path;      // the copy, inside snapshot.folder
    bool isFolder = false;
    qint64 size = 0;   // bytes; 0 for folders
    QDateTime modified; // the copy's modified time, local
};

// Btrfs snapshots as snapper lays them out, read from the file system only (no snapper, no
// root rights). Everything here blocks while it reads: callers that must stay responsive run
// it on a worker thread (see VersionModel). Safe to call from any thread.
class RILEYCORE_EXPORT Snapshots
{
public:
    // What every change inside a snapshot is refused with.
    static QString readOnlyMessage();

    // True when path lies in a snapshot folder: it is, or is below, a folder named
    // ".snapshots". Nothing there is ever written. Looks at the path only.
    static bool isInSnapshot(const QString &path);

    // The nearest folder at or above path (path itself when it is a folder) that holds snapshots (a .snapshots folder
    // with at least one <number>/info.xml beside <number>/snapshot/), on the same file system
    // as path (a Btrfs subvolume is its own). Empty when there is none, or path is itself in a
    // snapshot.
    static QString baseFor(const QString &path);

    // Every snapshot of base that can be read, oldest first (by date, then number).
    static QList<Snapshot> snapshotsOf(const QString &base);

    // Reads a snapper info.xml. Its <date> is UTC "YYYY-MM-DD hh:mm:ss"; the result is local
    // time. Nothing when the file is missing or says no date.
    static bool readInfo(const QString &infoPath, Snapshot *snapshot);

    // The earlier versions of a file or folder, newest first.
    //
    // A file: every snapshot holding a copy; snapshots in a row whose copies are the same
    // (same size and modified time, or the same bytes) count once, under the oldest date of
    // the run; copies the same as the live file are left out. A folder: every snapshot
    // holding it. A snapshot without a copy has no version. shouldStop is asked between
    // snapshots and while bytes are compared; when it says yes the result is empty.
    static QList<FileVersion> versionsOf(const QString &path, const std::function<bool()> &shouldStop = {});

    // The name Restore as Copy gives a version: "doc (2026-11-01 10.00).txt" for doc.txt and
    // a version of 2026-11-01 10:00 (local); folders and names without an extension get the
    // date at the end.
    static QString copyName(const QString &name, const QDateTime &date, bool isFolder);

    // True when the two paths hold the same thing: the same kind, and for files the same size
    // and modified time, or the same bytes; for symlinks the same target. Folders are never
    // compared and never count as the same.
    static bool sameContent(const QString &a, const QString &b, const std::function<bool()> &shouldStop = {});
};

} // namespace Riley
