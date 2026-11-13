// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

#include <atomic>
#include <optional>

namespace Riley {

class JobQueue;

// Something a job could not do. The job goes on with the rest.
struct JobFailure
{
    QString path;    // the file or folder it is about
    QString message; // for a person: names the file and says why
};

// One change a finished job made, in the order made, so it can be undone. Paths are absolute.
struct JobStep
{
    enum Kind {
        Created, // `to` was made (a copy, a new folder); undo sends it to the trash
        Moved,   // `from` now lives at `to` (a move or a rename); undo moves it back
        Trashed, // `from` went to the trash and lives at `to` (in Trash/files); undo restores it
        Restored, // `from` (in Trash/files) was put back at `to`; undo sends it to the trash again
        FolderRemoved, // the empty folder `from` was removed (a merging move emptied it); undo makes it again
    };
    Kind kind;
    QString from;
    QString to;
};

// One file operation, run off the UI thread by a JobQueue.
//
// The work runs on a worker thread; the object itself lives on the thread that made it and
// its signals arrive there. Its getters, stop() and answerClash() may be called from that
// thread at any time.
//
// Never lose a file: a job does not overwrite. When a target name is taken it asks (clashAsked)
// and waits for answerClash(). Replace first sends the old one to the trash; if that fails,
// nothing is replaced. stop() ends the job between files, or inside a file's data, and no
// partial file is left behind.
//
// Subclasses (copy, move, rename, new folder; trash, restore and delete for good are added the
// same way) implement run() with the protected helpers.
class RILEYCORE_EXPORT FileJob : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Kind kind READ kind CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 filesDone READ filesDone NOTIFY progressChanged)
    Q_PROPERTY(qint64 filesTotal READ filesTotal NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesDone READ bytesDone NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal NOTIFY progressChanged)
    Q_PROPERTY(QString clashSource READ clashSource NOTIFY clashAsked)
    Q_PROPERTY(QString clashTarget READ clashTarget NOTIFY clashAsked)
    Q_PROPERTY(bool clashCanMerge READ clashCanMerge NOTIFY clashAsked)
    Q_PROPERTY(int failureCount READ failureCount NOTIFY finished)

public:
    // Undo: puts back what another job did (an UndoHistory undo or redo).
    enum class Kind { Copy, Move, Rename, NewFolder, Trash, Restore, Delete, Undo };
    Q_ENUM(Kind)

    enum class State {
        Waiting, // queued behind another job
        Running,
        Asking,  // a name clash waits for answerClash()
        Done,    // finished; failures() lists what could not be done
        Stopped, // ended by stop(); steps() lists what was done before
    };
    Q_ENUM(State)

    enum class ClashChoice {
        Replace,  // the old one goes to the trash, then the new one takes its name
        KeepBoth, // the new one gets a free name: "a (2).txt"
        Skip,     // the old one stays, the new one is not copied or moved
        Merge,    // both are folders: the new one's items go into the old one, each clash asked again
    };
    Q_ENUM(ClashChoice)

    ~FileJob() override;

    virtual Kind kind() const = 0;

    State state() const { return m_state.load(); }
    bool isFinished() const { return state() == State::Done || state() == State::Stopped; }
    // On the job's own thread: finished() has been emitted, so the worker is done with the job
    // and it may be deleted.
    bool hasReportedFinish() const { return m_finishSeen; }

    qint64 filesDone() const { return m_filesDone.load(); }
    qint64 filesTotal() const { return m_filesTotal.load(); }
    qint64 bytesDone() const { return m_bytesDone.load(); }
    qint64 bytesTotal() const { return m_bytesTotal.load(); }

    QString clashSource() const;
    QString clashTarget() const;
    // Whether the clash being asked about is a folder meeting a folder, so Merge may be answered.
    bool clashCanMerge() const;

    QList<JobFailure> failures() const;
    int failureCount() const;
    QList<JobStep> steps() const;

    // Ends the job as soon as it can. Answers a waiting clash too.
    Q_INVOKABLE void stop();
    bool stopRequested() const { return m_stop.load(); }

    // Answers the clash being asked about. With forAll the same answer goes to every later
    // clash of this job without asking. Merge is ignored unless clashCanMerge(); a Merge given
    // for all applies to later folder clashes only, and a file clash is still asked.
    Q_INVOKABLE void answerClash(Riley::FileJob::ClashChoice choice, bool forAll = false);

signals:
    void stateChanged(Riley::FileJob::State state);
    // Throttled: at most every 50 ms while running, and always once at the end.
    void progressChanged(qint64 filesDone, qint64 filesTotal, qint64 bytesDone, qint64 bytesTotal);
    void clashAsked(const QString &source, const QString &target);
    void finished();

protected:
    explicit FileJob(QObject *parent = nullptr);

    // The work. Runs on the worker thread; returns when done or when stopRequested().
    virtual void run() = 0;

    // Asks about a clash and waits for the answer; nothing when the job was stopped meanwhile.
    // A "for all" answer given before is returned without asking. canMerge: both are folders,
    // so Merge is a possible answer; otherwise Merge is never returned.
    std::optional<ClashChoice> askClash(const QString &source, const QString &target, bool canMerge = false);

    void addFailure(const QString &path, const QString &message);
    void addStep(JobStep::Kind kind, const QString &from, const QString &to);
    // Drops the last step if it is exactly this one (a step recorded early, then undone by the
    // job itself, for example a partial copy removed again).
    void dropStep(JobStep::Kind kind, const QString &from, const QString &to);

    void setTotals(qint64 files, qint64 bytes);
    void addDone(qint64 files, qint64 bytes);

private:
    friend class JobQueue;
    void execute(); // called by JobQueue on its worker thread
    void setState(State state);
    void reportProgress(bool force);

    std::atomic<State> m_state{State::Waiting};
    std::atomic<bool> m_stop{false};
    std::atomic<qint64> m_filesDone{0}, m_filesTotal{0}, m_bytesDone{0}, m_bytesTotal{0};
    std::atomic<qint64> m_lastReport{0};
    bool m_finishSeen = false; // on the job's own thread: finished() was emitted

    mutable QMutex m_mutex; // guards everything below
    QWaitCondition m_answered;
    QString m_clashSource, m_clashTarget;
    bool m_clashCanMerge = false;
    std::optional<ClashChoice> m_answer;
    std::optional<ClashChoice> m_answerForAll;
    QList<JobFailure> m_failures;
    QList<JobStep> m_steps;
};

} // namespace Riley

Q_DECLARE_METATYPE(Riley::JobFailure)
Q_DECLARE_METATYPE(Riley::JobStep)
