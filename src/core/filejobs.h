// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "filejob.h"
#include "rileycore_export.h"

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QStringList>

struct stat;

namespace Riley {

// Copies or moves files and folders into a destination folder.
//
// Copy: the data goes through copy_file_range (a reflink on Btrfs, a server-side copy where the
// file system offers one), with plain reads and writes where it cannot. Mode and modified time
// are kept; symlinks are copied as symlinks, never followed. Each file is written under no name
// (or a hidden temporary one) and only gets its name once complete, so a stopped or failed copy
// leaves no partial file, and a name taken meanwhile is never overwritten.
//
// Move: a rename where it can; across drives (EXDEV) a full copy first, and the original is
// removed only once the copy is complete. If the copy fails or is stopped, what was copied is
// removed again and the original is untouched.
//
// A folder is never copied or moved into itself: that item is refused with a failure naming it,
// and nothing is made for it.
//
// A folder meeting a folder of the same name can be merged (ClashChoice::Merge): its items go
// into the existing folder one by one, each clash asked again, and every item added or replaced
// is its own step, so one undo takes the merge back. A merging move removes the emptied source
// folder (a FolderRemoved step).
class RILEYCORE_EXPORT TransferJob : public FileJob
{
    Q_OBJECT

public:
    QStringList sources() const { return m_sources; }
    QString destination() const { return m_destination; }

    // The paths the items were given in the destination ("a (2).txt" after Keep both), in the
    // order of sources; an item skipped or not done has none. Complete once the job finished.
    QStringList targets() const;

protected:
    TransferJob(const QStringList &sources, const QString &destination, bool move, QObject *parent);
    void run() override;

    bool m_pretendOtherDrive = false;

private:
    // Copies src (whose lstat is st) to dst, which does not exist. Records failures; returns true
    // only when everything below it was copied. With top set, records the Created step as soon as
    // dst exists.
    bool copyEntry(const QByteArray &src, const QByteArray &dst, const struct stat &st, bool top);
    bool copyFile(const QByteArray &src, const QByteArray &dst, const struct stat &st, bool top);
    bool copyFolder(const QByteArray &src, const QByteArray &dst, const struct stat &st, bool top);
    void transferOne(const QString &source);
    // Puts src (whose lstat is st) into folder, asking about a clash. Returns the path that now
    // holds it (the merged folder after Merge), or nothing when it was skipped or failed.
    QString place(const QString &srcPath, const struct stat &st, const QString &folder);
    // Merge: puts each item of the folder srcPath into the existing folder target, one by one.
    void mergeFolder(const QString &srcPath, const QString &target);

    const QStringList m_sources;
    const QString m_destination;
    const bool m_move;
    mutable QMutex m_targetsMutex;
    QStringList m_targets;
};

class RILEYCORE_EXPORT CopyJob : public TransferJob
{
    Q_OBJECT

public:
    CopyJob(const QStringList &sources, const QString &destination, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Copy; }
};

class RILEYCORE_EXPORT MoveJob : public TransferJob
{
    Q_OBJECT

public:
    MoveJob(const QStringList &sources, const QString &destination, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Move; }

    // For tests: every rename fails as if the destination were on another drive (EXDEV), so
    // the move takes its copy-then-remove path. Set before the job starts.
    void setPretendOtherDrive(bool pretend) { m_pretendOtherDrive = pretend; }
};

// Gives one file or folder a new name in the same folder. A name already taken is refused with
// a failure; nothing is overwritten.
class RILEYCORE_EXPORT RenameJob : public FileJob
{
    Q_OBJECT

public:
    RenameJob(const QString &path, const QString &newName, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Rename; }

    QString path() const { return m_path; }
    QString newPath() const { return m_newPath; }

protected:
    void run() override;

private:
    const QString m_path;
    const QString m_newName;
    const QString m_newPath;
};

// Makes a folder named name in parent, or "name (2)", "name (3)" ... when name is taken.
class RILEYCORE_EXPORT NewFolderJob : public FileJob
{
    Q_OBJECT

public:
    explicit NewFolderJob(const QString &parent, const QString &name = defaultName(), QObject *owner = nullptr);
    Kind kind() const override { return Kind::NewFolder; }

    static QString defaultName() { return QStringLiteral("New Folder"); }

    // The folder made; empty until the job is done, or if it failed.
    QString createdPath() const;

protected:
    void run() override;

private:
    const QString m_parent;
    const QString m_name;
    mutable QMutex m_createdMutex;
    QString m_created;
};

} // namespace Riley
