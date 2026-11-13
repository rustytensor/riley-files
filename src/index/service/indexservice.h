// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "indexdatabase.h"
#include "indexsettings.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QLocalServer;
class QThread;

namespace Riley {

class ChangeFollower;
class Indexer;

// The indexer's own thread: the database, the Indexer and the ChangeFollower live here, and the
// commands from the window reach them as queued calls. Indexer works synchronously, so a long
// scan holds this thread; stopNow() (from any thread) ends it between two folders first.
class IndexWorker : public QObject
{
    Q_OBJECT

public:
    IndexWorker(QString settingsFile, QString databaseFile, QString changesSocket);
    ~IndexWorker() override;

    // Thread-safe: ends the scan running, if any, between two folders.
    void stopNow();

public slots:
    // Opens the database, follows the helper's changes and starts: the first scan when the
    // index was never completed, a catch-up otherwise.
    void begin();
    // The settings file changed: what is indexed follows it.
    void reload();
    void pause();
    void resume();
    void rebuild();
    // Before the thread ends: the follower stopped, the database closed.
    void finish();

signals:
    // Indexing is off in the settings, or the database cannot be opened: the service ends.
    void quitAsked(int code);

private:
    void follow();

    QString m_settingsFile;
    QString m_databaseFile;
    QString m_changesSocket;
    IndexDatabase m_db;
    std::unique_ptr<Indexer> m_indexer;
    std::unique_ptr<ChangeFollower> m_follower;
    std::atomic<Indexer *> m_running = nullptr;
};

// The service: the worker on its thread, and the window's socket on the main thread.
class IndexService : public QObject
{
    Q_OBJECT

public:
    IndexService(QString settingsFile, QString databaseFile, QString socket, QString changesSocket);
    ~IndexService() override;

    // Listens on the socket (refusing when another service answers there) and starts the worker.
    bool start(QString &error);
    // Stops the worker cleanly; the event loop can end after.
    void shutDown();

signals:
    void quitAsked(int code);

private:
    void answer();
    QString command(const QString &line);

    QString m_databaseFile;
    QString m_socket;
    QLocalServer *m_server = nullptr;
    QThread *m_thread = nullptr;
    IndexWorker *m_worker = nullptr;
};

} // namespace Riley
