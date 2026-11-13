// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include <QList>
#include <QString>
#include <QtGlobal>

#include <atomic>

namespace Riley {

class GraphFlash;
class GraphTree;

// When each node of a tree first appeared, in seconds since 1970 (UTC): for a tree in a git
// repository, the author date of the commit that first added a file (one `git log` over the
// folder, run as an argument list); for anything git does not know, the file's birth time or its
// modified time, whichever is older. A folder is as old as the oldest thing in it, or its own time
// when it holds nothing with a time.
//
// Plain and blocking: call it from a worker thread.
class RILEYGRAPH_EXPORT GraphHistory
{
public:
    static QList<qint64> bornTimes(const GraphTree &tree, const std::atomic<bool> *stop = nullptr);
    // True when the last bornTimes() call on this thread found the tree in a git repository.
    static bool lastUsedGit();
};

// "Watch it grow": the tree's history replayed in 14 s, eased in and out (quadratic). Each node
// appears when the replayed time reaches its born time, with a flash of 1.
class RILEYGRAPH_EXPORT GrowthReplay
{
public:
    static constexpr double DurationMs = 14000;

    // Starts over with the given born times (one per node).
    void start(const QList<qint64> &born);
    void stop();
    bool running() const { return m_running; }
    // True once the replay has reached its end (it then stays on its last frame until stopped).
    bool finished() const { return m_running && m_elapsed >= DurationMs; }

    // 0..1 eased in-out quad of the share of the 14 s gone.
    static double ease(double share);
    // The replayed time after elapsedMs: first + (last - first) * ease(elapsed / 14 s).
    double timeAt(double elapsedMs) const;
    double now() const { return timeAt(m_elapsed); }
    double elapsedMs() const { return m_elapsed; }
    qint64 first() const { return m_first; }
    qint64 last() const { return m_last; }

    // Moves the replay on by dt ms; nodes that appear in this step get a flash of 1. Their
    // indices, in order.
    QList<int> step(double dtMs, GraphFlash *flash = nullptr);
    // Jumps to elapsedMs (for tests and seeking); nodes appearing on the way flash as in step().
    QList<int> seek(double elapsedMs, GraphFlash *flash = nullptr);

    // Whether a node is shown: always when not running, else once its time has come.
    bool shown(int node) const;
    int shownCount() const;
    // The month being replayed, "yyyy-MM" (UTC); empty when not running.
    QString caption() const;

private:
    QList<qint64> m_born;
    qint64 m_first = 0;
    qint64 m_last = 0;
    double m_elapsed = 0;
    double m_previous = 0; // the replayed time of the last step
    bool m_running = false;
};

} // namespace Riley
