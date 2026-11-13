// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "trashjobs.h"

#include "filenames.h"
#include "trash.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

namespace Riley {

namespace {

QString absolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

TrashJob::TrashJob(const QStringList &paths, QObject *parent)
    : FileJob(parent)
    , m_paths(paths)
{
}

QStringList TrashJob::trashedPaths() const
{
    QMutexLocker lock(&m_resultMutex);
    return m_trashed;
}

void TrashJob::run()
{
    setTotals(m_paths.size(), 0);
    for (const QString &given : m_paths) {
        if (stopRequested())
            return;
        const QString path = absolute(given);
        QString why;
        const std::optional<TrashEntry> entry = Trash::put(path, &why);
        if (entry) {
            addStep(JobStep::Trashed, path, entry->trashedPath);
            QMutexLocker lock(&m_resultMutex);
            m_trashed.append(entry->trashedPath);
        } else {
            addFailure(path, why);
        }
        addDone(1, 0);
    }
}

RestoreJob::RestoreJob(const QStringList &trashedPaths, QObject *parent)
    : FileJob(parent)
    , m_paths(trashedPaths)
{
}

RestoreJob::RestoreJob(const QStringList &trashedPaths, bool askOnClash, QObject *parent)
    : FileJob(parent)
    , m_paths(trashedPaths)
    , m_ask(askOnClash)
{
}

QStringList RestoreJob::restoredPaths() const
{
    QMutexLocker lock(&m_resultMutex);
    return m_restored;
}

void RestoreJob::run()
{
    setTotals(m_paths.size(), 0);
    for (const QString &given : m_paths) {
        if (stopRequested())
            return;
        const QString path = absolute(given);
        QString why;
        QString destination;
        const std::optional<TrashEntry> entry = m_ask ? Trash::entryFor(path) : std::nullopt;
        if (entry && pathExists(entry->originalPath)) {
            const QString target = entry->originalPath;
            const QFileInfo targetInfo(target);
            const bool isFolder = QFileInfo(path).isDir() && !QFileInfo(path).isSymLink();
            const std::optional<ClashChoice> choice = askClash(path, target);
            if (!choice)
                return; // stopped while asking
            if (*choice == ClashChoice::Skip) {
                addDone(1, 0);
                continue;
            }
            if (*choice == ClashChoice::Replace) {
                const std::optional<TrashEntry> old = Trash::put(target, &why);
                if (!old) {
                    addFailure(path, QStringLiteral("“%1” was not replaced: %2").arg(targetInfo.fileName(), why));
                    addDone(1, 0);
                    continue;
                }
                addStep(JobStep::Trashed, target, old->trashedPath);
            } else {
                destination = targetInfo.absolutePath() + QLatin1Char('/')
                              + freeName(targetInfo.absolutePath(), targetInfo.fileName(), isFolder);
            }
        }
        const std::optional<QString> back = Trash::restore(path, destination, &why);
        if (back) {
            addStep(JobStep::Restored, path, *back);
            QMutexLocker lock(&m_resultMutex);
            m_restored.append(*back);
        } else {
            addFailure(path, why);
        }
        addDone(1, 0);
    }
}

DeleteJob::DeleteJob(const QStringList &paths, QObject *parent)
    : FileJob(parent)
    , m_paths(paths)
{
}

void DeleteJob::run()
{
    setTotals(m_paths.size(), 0);
    for (const QString &given : m_paths) {
        if (stopRequested())
            return;
        const QString path = absolute(given);
        QString why;
        const bool inTrash = Trash::isInTrash(path);
        if (!(inTrash ? Trash::erase(path, &why) : Trash::removeForGood(path, &why)))
            addFailure(path, why);
        addDone(1, 0);
    }
}

} // namespace Riley
