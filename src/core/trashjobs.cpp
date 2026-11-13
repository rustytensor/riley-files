// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "trashjobs.h"

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
        const std::optional<QString> back = Trash::restore(path, QString(), &why);
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
    const QString filesDir = Trash::homeTrashPath() + QStringLiteral("/files");
    for (const QString &given : m_paths) {
        if (stopRequested())
            return;
        const QString path = absolute(given);
        QString why;
        const bool inTrash = QFileInfo(path).absolutePath() == filesDir;
        if (!(inTrash ? Trash::erase(path, &why) : Trash::removeForGood(path, &why)))
            addFailure(path, why);
        addDone(1, 0);
    }
}

} // namespace Riley
