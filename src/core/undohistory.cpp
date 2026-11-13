// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "undohistory.h"

#include "filenames.h"
#include "jobqueue.h"
#include "trash.h"

#include <QDir>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

namespace Riley {

namespace {

QString nameOf(const QString &path)
{
    return QFileInfo(path).fileName();
}

QString parentOf(const QString &path)
{
    return QFileInfo(path).absolutePath();
}

// The file system as it will be once the steps checked so far are put back, so a later step
// can be checked against what the earlier ones change (a Replace: the new copy goes to the
// trash, and only then is the old one's name free again).
class Plan
{
public:
    bool exists(const QString &path) const
    {
        if (m_made.contains(path))
            return true;
        for (const QString &gone : m_gone) {
            if (path == gone || path.startsWith(gone + QLatin1Char('/')))
                return false;
        }
        return pathExists(path);
    }
    bool isFolder(const QString &path) const
    {
        return m_made.contains(path) || (exists(path) && QFileInfo(path).isDir());
    }
    void remove(const QString &path)
    {
        m_made.remove(path);
        m_gone.insert(path);
    }
    void add(const QString &path)
    {
        m_gone.remove(path);
        m_made.insert(path);
    }

private:
    QSet<QString> m_gone, m_made;
};

// Checks that step can be put back once the ones after it have been; says why not.
bool check(const JobStep &step, Plan *plan, QString *why)
{
    switch (step.kind) {
    case JobStep::Created:
    case JobStep::Restored:
        if (!plan->exists(step.to)) {
            *why = QStringLiteral("“%1” is not there any more").arg(nameOf(step.to));
            return false;
        }
        plan->remove(step.to);
        return true;
    case JobStep::Moved:
    case JobStep::Trashed:
        if (!plan->exists(step.to)) {
            *why = step.kind == JobStep::Trashed
                       ? QStringLiteral("“%1” is no longer in the trash").arg(nameOf(step.from))
                       : QStringLiteral("“%1” is not there any more").arg(nameOf(step.to));
            return false;
        }
        if (plan->exists(step.from)) {
            *why = QStringLiteral("There is already something named “%1” in “%2”")
                       .arg(nameOf(step.from), nameOf(parentOf(step.from)));
            return false;
        }
        // A thing restored from the trash gets its folder made again; a moved one does not.
        if (step.kind == JobStep::Moved && !plan->isFolder(parentOf(step.from))) {
            *why = QStringLiteral("The folder “%1” is not there any more").arg(nameOf(parentOf(step.from)));
            return false;
        }
        plan->remove(step.to);
        plan->add(step.from);
        return true;
    case JobStep::FolderRemoved:
        if (plan->exists(step.from)) {
            *why = QStringLiteral("There is already something named “%1” in “%2”")
                       .arg(nameOf(step.from), nameOf(parentOf(step.from)));
            return false;
        }
        if (!plan->isFolder(parentOf(step.from))) {
            *why = QStringLiteral("The folder “%1” is not there any more").arg(nameOf(parentOf(step.from)));
            return false;
        }
        plan->add(step.from);
        return true;
    }
    return false;
}

} // namespace

UndoJob::UndoJob(const QList<JobStep> &steps, bool redo, QObject *parent)
    : FileJob(parent)
    , m_steps(steps)
    , m_redo(redo)
{
}

void UndoJob::run()
{
    const QString cannot = m_redo ? QStringLiteral("Cannot redo") : QStringLiteral("Cannot undo");
    setTotals(m_steps.size(), 0);

    Plan plan;
    for (qsizetype i = m_steps.size() - 1; i >= 0; --i) {
        QString why;
        if (!check(m_steps.at(i), &plan, &why)) {
            m_refused.store(true);
            addFailure(m_steps.at(i).to, QStringLiteral("%1: %2").arg(cannot, why));
            return;
        }
    }

    for (qsizetype i = m_steps.size() - 1; i >= 0; --i) {
        if (stopRequested())
            return;
        const JobStep &step = m_steps.at(i);
        QString why;
        switch (step.kind) {
        case JobStep::Created:
        case JobStep::Restored:
            if (const std::optional<TrashEntry> entry = Trash::put(step.to, &why))
                addStep(JobStep::Trashed, step.to, entry->trashedPath);
            break;
        case JobStep::Moved:
            if (const int e = renameNoReplace(step.to, step.from); e == 0)
                addStep(JobStep::Moved, step.to, step.from);
            else if (e == EXDEV)
                why = QStringLiteral("“%1” is on another drive now").arg(nameOf(step.to));
            else
                why = QStringLiteral("“%1” cannot be moved back: %2")
                          .arg(nameOf(step.to), QString::fromLocal8Bit(std::strerror(e)));
            break;
        case JobStep::Trashed:
            if (const std::optional<QString> back = Trash::restore(step.to, step.from, &why))
                addStep(JobStep::Restored, step.to, *back);
            break;
        case JobStep::FolderRemoved:
            if (QDir().mkdir(step.from))
                addStep(JobStep::Created, QString(), step.from);
            else
                why = QStringLiteral("The folder “%1” cannot be made again").arg(nameOf(step.from));
            break;
        }
        if (!why.isEmpty()) {
            // Something changed between the check and now: stop here, the rest stays undoable.
            addFailure(step.to, QStringLiteral("%1: %2").arg(cannot, why));
            return;
        }
        m_undone.fetch_add(1);
        addDone(1, 0);
    }
}

UndoHistory::UndoHistory(JobQueue *queue, QObject *parent)
    : QObject(parent)
    , m_queue(queue)
{
    connect(queue, &JobQueue::jobFinished, this, &UndoHistory::record);
}

QString UndoHistory::kindText(FileJob::Kind kind)
{
    switch (kind) {
    case FileJob::Kind::Copy: return QStringLiteral("Copy");
    case FileJob::Kind::Move: return QStringLiteral("Move");
    case FileJob::Kind::Rename: return QStringLiteral("Rename");
    case FileJob::Kind::NewFolder: return QStringLiteral("New Folder");
    case FileJob::Kind::Trash: return QStringLiteral("Move to Trash");
    case FileJob::Kind::Restore: return QStringLiteral("Restore from Trash");
    case FileJob::Kind::Delete: return QStringLiteral("Delete");
    case FileJob::Kind::Undo: break;
    }
    return QString();
}

QString UndoHistory::undoText() const
{
    return m_undo.isEmpty() ? QString() : QStringLiteral("Undo %1").arg(kindText(m_undo.last().kind));
}

QString UndoHistory::redoText() const
{
    return m_redo.isEmpty() ? QString() : QStringLiteral("Redo %1").arg(kindText(m_redo.last().kind));
}

void UndoHistory::push(QList<Entry> *stack, const Entry &entry)
{
    stack->append(entry);
    while (stack->size() > limit)
        stack->removeFirst();
}

void UndoHistory::record(FileJob *job)
{
    if (!job || m_own.remove(job)) // an undo or redo of ours: handled where it was started
        return;
    const QList<JobStep> steps = job->steps();
    if (steps.isEmpty())
        return;
    push(&m_undo, Entry{job->kind(), steps});
    m_redo.clear();
    ++m_generation;
    emit changed();
}

FileJob *UndoHistory::undo()
{
    return runEntry(false);
}

FileJob *UndoHistory::redo()
{
    return runEntry(true);
}

FileJob *UndoHistory::runEntry(bool redo)
{
    QList<Entry> &from = redo ? m_redo : m_undo;
    if (from.isEmpty())
        return nullptr;
    const Entry entry = from.takeLast();
    auto *job = new UndoJob(entry.steps, redo);
    m_own.insert(job);
    const quint64 generation = m_generation;
    connect(job, &FileJob::finished, this, [this, job, entry, redo, generation] {
        const qsizetype undone = job->undoneCount();
        const qsizetype left = entry.steps.size() - undone;
        // What could not be put back stays where it was, unless it was refused: then it never
        // can be, as things are now.
        if (left > 0 && !job->wasRefused())
            push(redo ? &m_redo : &m_undo, Entry{entry.kind, entry.steps.first(left)});
        const QList<JobStep> made = job->steps();
        if (!made.isEmpty()) {
            if (redo)
                push(&m_undo, Entry{entry.kind, made});
            else if (generation == m_generation)
                push(&m_redo, Entry{entry.kind, made});
        }
        const QList<JobFailure> failures = job->failures();
        if (!failures.isEmpty())
            emit refused(failures.first().message);
        emit changed();
    });
    m_queue->start(job);
    emit changed();
    return job;
}

void UndoHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
    ++m_generation;
    emit changed();
}

} // namespace Riley
