// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "jobqueue.h"

namespace Riley {

JobQueue::JobQueue(QObject *parent)
    : QObject(parent)
{
    // One at a time: two copies to the same disk at once are slower than one after the other,
    // and a later job may depend on an earlier one (paste, then rename).
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

JobQueue::~JobQueue()
{
    stopAll();
    m_pool.waitForDone();
}

FileJob *JobQueue::start(FileJob *job)
{
    if (!job)
        return nullptr;
    job->setParent(this);
    m_jobs.append(job);
    connect(job, &FileJob::finished, this, [this, job] {
        emit jobFinished(job);
        emit activeCountChanged();
    });
    emit jobStarted(job);
    emit activeCountChanged();
    m_pool.start([job] { job->execute(); });
    return job;
}

QList<FileJob *> JobQueue::jobs() const
{
    QList<FileJob *> list;
    for (const QPointer<FileJob> &job : m_jobs) {
        if (job)
            list.append(job.data());
    }
    return list;
}

int JobQueue::activeCount() const
{
    int count = 0;
    for (const QPointer<FileJob> &job : m_jobs) {
        if (job && !job->isFinished())
            ++count;
    }
    return count;
}

void JobQueue::forget(FileJob *job)
{
    if (!job || !job->hasReportedFinish())
        return;
    m_jobs.removeAll(job);
    // Its last queued signals may still be on their way; deleteLater drops them safely.
    job->deleteLater();
}

void JobQueue::forgetFinished()
{
    const QList<FileJob *> all = jobs();
    for (FileJob *job : all) {
        if (job->hasReportedFinish())
            forget(job);
    }
}

void JobQueue::stopAll()
{
    for (const QPointer<FileJob> &job : std::as_const(m_jobs)) {
        if (job)
            job->stop();
    }
}

bool JobQueue::waitForIdle(int msecs)
{
    return m_pool.waitForDone(msecs);
}

} // namespace Riley
