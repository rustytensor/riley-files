// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "versionjobs.h"

#include "filenames.h"
#include "snapshots.h"
#include "trash.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace Riley {

namespace {

QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

VersionRestoreJob::VersionRestoreJob(const QString &copyPath, const QString &livePath, const QDateTime &date,
                                     Mode mode, QObject *parent)
    : TransferJob({cleanAbsolute(copyPath)}, QFileInfo(cleanAbsolute(livePath)).absolutePath(), false, parent)
    , m_copyPath(cleanAbsolute(copyPath))
    , m_livePath(cleanAbsolute(livePath))
    , m_date(date)
    , m_mode(mode)
{
}

QString VersionRestoreJob::restoredPath() const
{
    const QStringList made = targets();
    return made.isEmpty() ? QString() : made.first();
}

void VersionRestoreJob::run()
{
    const QString name = QFileInfo(m_livePath).fileName();
    const QString folder = QFileInfo(m_livePath).absolutePath();
    if (Snapshots::isInSnapshot(m_livePath)) {
        addFailure(m_livePath, Snapshots::readOnlyMessage());
        return;
    }
    if (!pathExists(m_copyPath)) {
        addFailure(m_copyPath, QCoreApplication::translate("VersionJobs", "This version of “%1” is not there any more").arg(name));
        return;
    }
    const bool isFolder = QFileInfo(m_copyPath).isDir() && !QFileInfo(m_copyPath).isSymLink();
    if (m_mode == Mode::Restore) {
        if (pathExists(m_livePath)) {
            QString why;
            const std::optional<TrashEntry> trashed = Trash::put(m_livePath, &why);
            if (!trashed) {
                addFailure(m_livePath, QCoreApplication::translate("VersionJobs", "“%1” was not restored: %2").arg(name, why));
                return;
            }
            addStep(JobStep::Trashed, m_livePath, trashed->trashedPath);
        }
        m_topName = name;
    } else {
        m_topName = freeName(folder, Snapshots::copyName(name, m_date, isFolder), isFolder);
    }
    TransferJob::run();
}

} // namespace Riley
