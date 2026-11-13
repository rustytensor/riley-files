// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileythumbs_export.h"

#include <QImage>
#include <QMutex>
#include <QRecursiveMutex>
#include <QSet>
#include <QString>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>

namespace Riley {

class InstalledThumbnailers;

// What a request ended with.
struct RILEYTHUMBS_EXPORT Thumbnail {
    enum class Status {
        Made,    // `image` is the thumbnail (read from the cache or made just now)
        Failed,  // the file cannot be done; a fail entry says so and it is not tried again
        None,    // nothing can make one for this kind of file (or it is not a file)
    };
    Status status = Status::None;
    QString path;           // the file asked about
    QString thumbnailFile;  // the cached PNG when there is one
    QImage image;
};

// Gives thumbnails of files: from the freedesktop cache when one is there and current, else
// made and stored there. Images are made with QImageReader, other kinds with the installed
// .thumbnailer programs; a file that cannot be done gets a fail entry. All of this happens on
// worker threads, at most maxRunning() at a time, so the caller's thread never waits on a disk
// or a decoder. Requests are worked through in the order they came; one that is cancelled
// before its turn costs nothing (the views cancel tiles that leave the screen).
//
// Every function may be called from any thread.
class RILEYTHUMBS_EXPORT Thumbnailer
{
public:
    using Done = std::function<void(const Thumbnail &)>;

    Thumbnailer();
    // Drops what has not started and waits for what has.
    ~Thumbnailer();

    Thumbnailer(const Thumbnailer &) = delete;
    Thumbnailer &operator=(const Thumbnailer &) = delete;

    static constexpr int maxRunning() { return 4; }

    // Asks for the thumbnail of `path` for a tile of `wanted` pixels (the cache's size class for
    // it: 128 or 256). `done` is called once, on a worker thread, unless the ticket is cancelled
    // first. Returns the ticket.
    quint64 request(const QString &path, int wanted, Done done);
    // After this returns, `done` of that ticket is not running and will not be called.
    void cancel(quint64 ticket);

    // The same work, done now on the calling thread.
    Thumbnail make(const QString &path, int wanted);

    // Waits until nothing is queued or running.
    void waitForDone();

    // For tests: the most requests that were ever worked on at once, and how many times a
    // thumbnail was made or tried (cache hits and known failures do not count).
    int peakRunning() const { return m_peak.load(); }
    int attempts() const { return m_attempts.load(); }
    // Every thread a thumbnail was looked up or made on (by make(), whoever called it). A test
    // can check that the UI thread is never among them.
    QSet<Qt::HANDLE> workThreads() const;

private:
    struct Job {
        quint64 ticket = 0;
        QString path;
        int wanted = 0;
        Done done;
    };

    void startWorkers();  // with m_lock held
    void work();
    const InstalledThumbnailers &installed();
    QImage fromImageReader(const QString &path, int bucket) const;
    QImage fromInstalled(const QString &path, const QString &mimeType, int bucket);

    QMutex m_lock;  // the queue, the tickets, the workers count
    std::deque<Job> m_pending;
    QSet<quint64> m_live;
    quint64 m_nextTicket = 1;
    int m_workers = 0;
    bool m_stopping = false;

    // Held while a `done` runs, so cancel() can wait for one in progress.
    QRecursiveMutex m_delivering;

    QMutex m_installedLock;
    std::unique_ptr<InstalledThumbnailers> m_installed;

    std::atomic<int> m_running{0};
    std::atomic<int> m_peak{0};
    std::atomic<int> m_attempts{0};

    mutable QMutex m_threadsLock;
    QSet<Qt::HANDLE> m_threads;

    QThreadPool m_pool;
};

} // namespace Riley
