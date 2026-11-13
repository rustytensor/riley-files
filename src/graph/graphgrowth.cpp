// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphgrowth.h"

#include "gitlog.h"
#include "graphmotion.h"
#include "graphtree.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Riley {

namespace {

thread_local bool usedGit = false;

// The older of a path's birth and modified times, in seconds since 1970; -1 when neither is known.
qint64 fileTime(const QString &path)
{
    const QFileInfo info(path);
    qint64 best = -1;
    for (const QDateTime &when : {info.birthTime(), info.lastModified()}) {
        if (!when.isValid())
            continue;
        const qint64 secs = when.toSecsSinceEpoch();
        best = best < 0 ? secs : std::min(best, secs);
    }
    return best;
}

// Repository-relative path -> the author date of the commit that first added it.
QHash<QString, qint64> firstAdded(const QString &repository, const QString &folder, const std::atomic<bool> *stop)
{
    QHash<QString, qint64> added;
    QString relative = QDir(repository).relativeFilePath(folder);
    if (relative.isEmpty())
        relative = QStringLiteral(".");
    QProcess git;
    git.setProgram(QStringLiteral("git"));
    git.setArguments({QStringLiteral("-C"), repository, QStringLiteral("-c"), QStringLiteral("core.quotePath=false"),
                      QStringLiteral("log"), QStringLiteral("--reverse"), QStringLiteral("--no-renames"),
                      QStringLiteral("--diff-filter=A"), QStringLiteral("--format=@%at"), QStringLiteral("--name-only"),
                      QStringLiteral("HEAD"), QStringLiteral("--"), relative});
    git.setStandardInputFile(QProcess::nullDevice());
    git.start();
    if (!git.waitForStarted(5000))
        return added;
    QByteArray out;
    for (;;) {
        if (stop && stop->load()) {
            git.kill();
            git.waitForFinished(1000);
            return {};
        }
        const bool done = git.waitForFinished(100);
        out += git.readAllStandardOutput();
        if (done || git.state() == QProcess::NotRunning)
            break;
    }
    out += git.readAllStandardOutput();
    if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0)
        return added;
    qint64 when = -1;
    for (const QByteArray &raw : out.split('\n')) {
        if (raw.isEmpty())
            continue;
        if (raw.startsWith('@')) {
            bool ok = false;
            const qint64 secs = raw.mid(1).trimmed().toLongLong(&ok);
            when = ok ? secs : -1;
            continue;
        }
        if (when < 0)
            continue;
        const QString path = QString::fromUtf8(raw);
        const auto at = added.find(path);
        if (at == added.end())
            added.insert(path, when);
        else
            *at = std::min(*at, when);
    }
    return added;
}

} // namespace

QList<qint64> GraphHistory::bornTimes(const GraphTree &tree, const std::atomic<bool> *stop)
{
    usedGit = false;
    const int n = tree.size();
    QList<qint64> best(n, -1);
    if (n == 0)
        return best;

    const QString repository = GitLog::repositoryOf(tree.root());
    if (!repository.isEmpty()) {
        const QHash<QString, qint64> added = firstAdded(repository, tree.root(), stop);
        usedGit = !added.isEmpty();
        const QDir top(repository);
        for (auto it = added.cbegin(); it != added.cend(); ++it) {
            // The node of the file, or of the nearest folder above it that the graph shows.
            QString path = QDir::cleanPath(top.filePath(it.key()));
            int node = tree.indexOf(path);
            while (node < 0 && path.size() > tree.root().size()) {
                path = QFileInfo(path).path();
                node = tree.indexOf(path);
            }
            if (node >= 0)
                best[node] = best.at(node) < 0 ? it.value() : std::min(best.at(node), it.value());
        }
    }
    if (stop && stop->load())
        return best;

    // Every descendant comes after its folder, so going backwards each node is complete when it is
    // reached: it takes its own time if nothing gave it one, then hands it to its folder.
    for (int i = n - 1; i >= 0; --i) {
        if (best.at(i) < 0)
            best[i] = fileTime(tree.node(i).path);
        const int parent = tree.node(i).parent;
        if (parent >= 0 && best.at(i) >= 0)
            best[parent] = best.at(parent) < 0 ? best.at(i) : std::min(best.at(parent), best.at(i));
    }
    return best;
}

bool GraphHistory::lastUsedGit()
{
    return usedGit;
}

void GrowthReplay::start(const QList<qint64> &born)
{
    m_born = born;
    m_first = m_last = 0;
    bool any = false;
    for (qint64 t : born) {
        if (t < 0)
            continue;
        m_first = any ? std::min(m_first, t) : t;
        m_last = any ? std::max(m_last, t) : t;
        any = true;
    }
    m_elapsed = 0;
    m_previous = -std::numeric_limits<double>::infinity();
    m_running = true;
}

void GrowthReplay::stop()
{
    m_running = false;
    m_elapsed = 0;
}

double GrowthReplay::ease(double share)
{
    const double t = std::clamp(share, 0.0, 1.0);
    return t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) / 2;
}

double GrowthReplay::timeAt(double elapsedMs) const
{
    return m_first + double(m_last - m_first) * ease(elapsedMs / DurationMs);
}

QList<int> GrowthReplay::step(double dtMs, GraphFlash *flash)
{
    return seek(m_elapsed + std::max(0.0, dtMs), flash);
}

QList<int> GrowthReplay::seek(double elapsedMs, GraphFlash *flash)
{
    QList<int> appeared;
    if (!m_running)
        return appeared;
    m_elapsed = std::clamp(elapsedMs, 0.0, DurationMs);
    const double today = now();
    for (int i = 0; i < m_born.size(); ++i) {
        const double b = double(m_born.at(i));
        if (b > m_previous && b <= today) {
            appeared.append(i);
            if (flash)
                flash->raise(i, GraphFlash::OnAppearing);
        }
    }
    m_previous = std::max(m_previous, today);
    return appeared;
}

bool GrowthReplay::shown(int node) const
{
    if (!m_running)
        return true;
    return node >= 0 && node < m_born.size() && double(m_born.at(node)) <= now();
}

int GrowthReplay::shownCount() const
{
    int count = 0;
    for (int i = 0; i < m_born.size(); ++i)
        count += shown(i) ? 1 : 0;
    return count;
}

QString GrowthReplay::caption() const
{
    if (!m_running)
        return {};
    const auto secs = qint64(std::floor(now()));
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM"));
}

} // namespace Riley
