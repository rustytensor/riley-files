// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "filejob.h"
#include "rileycore_export.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>

namespace Riley {

class JobQueue;

// Puts back what a finished job did: its steps, last first (see JobStep).
//
//   Created  -> the thing made goes to the trash
//   Moved    -> it is moved back (a rename too)
//   Trashed  -> it comes back from the trash, and its trash entry goes
//   Restored -> it goes to the trash again
//
// Every step is checked before anything changes: when a file was renamed, moved or deleted
// since, or its old name has been taken, the undo is refused with a failure that says why, and
// nothing is touched. The steps this job makes are themselves undoable, which is how redo works.
class RILEYCORE_EXPORT UndoJob : public FileJob
{
    Q_OBJECT

public:
    // redo only changes the wording of the messages.
    UndoJob(const QList<JobStep> &steps, bool redo, QObject *parent = nullptr);
    Kind kind() const override { return Kind::Undo; }

    bool isRedo() const { return m_redo; }
    // How many of the given steps were put back, counted from the last.
    int undoneCount() const { return m_undone.load(); }
    // The check before the first change failed: nothing was touched.
    bool wasRefused() const { return m_refused.load(); }

protected:
    void run() override;

private:
    const QList<JobStep> m_steps;
    const bool m_redo;
    std::atomic<int> m_undone{0};
    std::atomic<bool> m_refused{false};
};

// The undo and redo history of the file jobs run by one queue. Each finished job that changed
// something (Done or Stopped, with steps) becomes one entry; a new entry clears what could be
// redone. undo() and redo() run an UndoJob on the same queue, so they wait for jobs before them.
class RILEYCORE_EXPORT UndoHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(QString redoText READ redoText NOTIFY changed)

public:
    // Entries kept; the oldest go first.
    static constexpr int limit = 100;

    explicit UndoHistory(JobQueue *queue, QObject *parent = nullptr);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int undoCount() const { return int(m_undo.size()); }
    int redoCount() const { return int(m_redo.size()); }
    // For a menu: "Undo Rename", "Redo Move to Trash"; empty when there is nothing.
    QString undoText() const;
    QString redoText() const;

    // Starts undoing the last entry, or redoing the last undone one. Returns the job (owned by
    // the queue), or nullptr when there is nothing to do. The entry leaves the history at once,
    // so a second press goes to the one before; what the job could not put back returns to it.
    Q_INVOKABLE Riley::FileJob *undo();
    Q_INVOKABLE Riley::FileJob *redo();

    void clear();

signals:
    void changed();
    // An undo or redo could not be done, or not all of it. For a person.
    void refused(const QString &message);

private:
    struct Entry
    {
        FileJob::Kind kind;
        QList<JobStep> steps;
    };

    void record(FileJob *job);
    FileJob *runEntry(bool redo);
    void push(QList<Entry> *stack, const Entry &entry);
    static QString kindText(FileJob::Kind kind);

    JobQueue *m_queue;
    QList<Entry> m_undo, m_redo;
    QSet<const FileJob *> m_own;
    quint64 m_generation = 0; // counts new entries, so a late undo does not bring back a stale redo
};

} // namespace Riley
