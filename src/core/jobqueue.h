// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "filejob.h"
#include "rileycore_export.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QThreadPool>

namespace Riley {

// Runs file jobs one after another on a worker thread, in the order they were added, so the UI
// thread never waits on the disk. The queue owns its jobs; a finished job stays until
// forget()/forgetFinished() so its steps and failures can still be read (the undo history
// copies the steps of each finished job; see jobFinished).
class RILEYCORE_EXPORT JobQueue : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)

public:
    explicit JobQueue(QObject *parent = nullptr);
    // Stops every job and waits for the worker to finish.
    ~JobQueue() override;

    // Takes the job (it becomes a child of the queue) and runs it after those before it.
    FileJob *start(FileJob *job);

    QList<FileJob *> jobs() const;
    // Jobs waiting or running.
    int activeCount() const;

    // Deletes a finished job. Does nothing to a job still waiting or running.
    void forget(FileJob *job);
    void forgetFinished();

    // Stops every job, waiting ones included.
    Q_INVOKABLE void stopAll();
    // Waits until no job is waiting or running, up to msecs (-1: no limit). Blocks the caller.
    bool waitForIdle(int msecs = -1);

signals:
    void jobStarted(Riley::FileJob *job);
    // Emitted on the queue's thread once the job is Done or Stopped.
    void jobFinished(Riley::FileJob *job);
    void activeCountChanged();

private:
    QThreadPool m_pool;
    QList<QPointer<FileJob>> m_jobs;
};

} // namespace Riley
