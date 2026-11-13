// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "filejob.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <chrono>

namespace Riley {

namespace {

qint64 nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

constexpr qint64 progressEveryMs = 50;

} // namespace

// Every signal is emitted on the job's own thread, never on the worker: receivers (views, a
// test's QSignalSpy) then never see a call from another thread. Posting from one thread keeps
// the order, so progress never appears to go back.

FileJob::FileJob(QObject *parent)
    : QObject(parent)
{
}

FileJob::~FileJob() = default;

QString FileJob::clashSource() const
{
    QMutexLocker lock(&m_mutex);
    return m_clashSource;
}

QString FileJob::clashTarget() const
{
    QMutexLocker lock(&m_mutex);
    return m_clashTarget;
}

QList<JobFailure> FileJob::failures() const
{
    QMutexLocker lock(&m_mutex);
    return m_failures;
}

int FileJob::failureCount() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_failures.size());
}

QList<JobStep> FileJob::steps() const
{
    QMutexLocker lock(&m_mutex);
    return m_steps;
}

void FileJob::stop()
{
    QMutexLocker lock(&m_mutex);
    m_stop.store(true);
    m_answered.wakeAll();
}

void FileJob::answerClash(ClashChoice choice, bool forAll)
{
    QMutexLocker lock(&m_mutex);
    if (m_state.load() != State::Asking)
        return;
    m_answer = choice;
    if (forAll)
        m_answerForAll = choice;
    m_answered.wakeAll();
}

std::optional<FileJob::ClashChoice> FileJob::askClash(const QString &source, const QString &target)
{
    QMutexLocker lock(&m_mutex);
    if (m_answerForAll)
        return m_answerForAll;
    if (m_stop.load())
        return std::nullopt;
    m_clashSource = source;
    m_clashTarget = target;
    m_answer.reset();
    m_state.store(State::Asking);
    QMetaObject::invokeMethod(this, [this, source, target] {
        emit stateChanged(State::Asking);
        emit clashAsked(source, target);
    }, Qt::QueuedConnection);
    while (!m_answer && !m_stop.load())
        m_answered.wait(&m_mutex);
    const std::optional<ClashChoice> answer = m_stop.load() ? std::nullopt : m_answer;
    m_clashSource.clear();
    m_clashTarget.clear();
    m_answer.reset();
    m_state.store(State::Running);
    QMetaObject::invokeMethod(this, [this] { emit stateChanged(State::Running); }, Qt::QueuedConnection);
    return answer;
}

void FileJob::addFailure(const QString &path, const QString &message)
{
    QMutexLocker lock(&m_mutex);
    m_failures.append(JobFailure{path, message});
}

void FileJob::addStep(JobStep::Kind kind, const QString &from, const QString &to)
{
    QMutexLocker lock(&m_mutex);
    m_steps.append(JobStep{kind, from, to});
}

void FileJob::dropStep(JobStep::Kind kind, const QString &from, const QString &to)
{
    QMutexLocker lock(&m_mutex);
    if (!m_steps.isEmpty() && m_steps.last().kind == kind && m_steps.last().from == from && m_steps.last().to == to)
        m_steps.removeLast();
}

void FileJob::setTotals(qint64 files, qint64 bytes)
{
    m_filesTotal.store(files);
    m_bytesTotal.store(bytes);
    reportProgress(true);
}

void FileJob::addDone(qint64 files, qint64 bytes)
{
    m_filesDone.fetch_add(files);
    m_bytesDone.fetch_add(bytes);
    reportProgress(false);
}

void FileJob::reportProgress(bool force)
{
    const qint64 now = nowMs();
    if (!force && now - m_lastReport.load() < progressEveryMs)
        return;
    m_lastReport.store(now);
    const qint64 fd = m_filesDone.load(), ft = m_filesTotal.load();
    const qint64 bd = m_bytesDone.load(), bt = m_bytesTotal.load();
    QMetaObject::invokeMethod(this, [this, fd, ft, bd, bt] { emit progressChanged(fd, ft, bd, bt); },
                              Qt::QueuedConnection);
}

void FileJob::setState(State state)
{
    m_state.store(state);
    QMetaObject::invokeMethod(this, [this, state] { emit stateChanged(state); }, Qt::QueuedConnection);
}

void FileJob::execute()
{
    if (!m_stop.load()) {
        setState(State::Running);
        run();
    }
    reportProgress(true);
    const State end = m_stop.load() ? State::Stopped : State::Done;
    m_state.store(end);
    // The last touch of the job from the worker: once this arrives, the job may be deleted.
    QMetaObject::invokeMethod(this, [this, end] {
        m_finishSeen = true;
        emit stateChanged(end);
        emit finished();
    }, Qt::QueuedConnection);
}

} // namespace Riley
