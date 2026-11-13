// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "indexservice.h"

#include "changefollower.h"
#include "indexer.h"
#include "indexerclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

#include <sys/stat.h>

namespace Riley {

IndexWorker::IndexWorker(QString settingsFile, QString databaseFile, QString changesSocket)
    : m_settingsFile(std::move(settingsFile))
    , m_databaseFile(std::move(databaseFile))
    , m_changesSocket(std::move(changesSocket))
{
}

IndexWorker::~IndexWorker() = default;

void IndexWorker::stopNow()
{
    if (Indexer *indexer = m_running.load())
        indexer->requestStop();
}

void IndexWorker::begin()
{
    const IndexSettings settings = IndexSettings::loadFile(m_settingsFile);
    if (!settings.enabled) {
        emit quitAsked(0);
        return;
    }
    if (!m_db.open(m_databaseFile, IndexDatabase::ReadWrite)) {
        qWarning("riley-files-indexer: %s", qPrintable(m_db.errorString()));
        emit quitAsked(1);
        return;
    }
    m_indexer = std::make_unique<Indexer>(m_db, settings);
    m_running = m_indexer.get();
    follow();
    // Paused before a first scan ended: Resume starts it.
    if (!m_indexer->isPaused() || m_db.state() == QLatin1String("done"))
        m_indexer->start();
}

void IndexWorker::follow()
{
    if (m_follower)
        m_follower->stop();
    m_follower = std::make_unique<ChangeFollower>(*m_indexer, m_changesSocket);
    m_follower->start();
}

void IndexWorker::reload()
{
    if (!m_indexer) {
        begin();
        return;
    }
    const IndexSettings settings = IndexSettings::loadFile(m_settingsFile);
    if (!settings.enabled) {
        emit quitAsked(0);
        return;
    }
    if (settings == m_indexer->settings())
        return;
    const bool newFolders = settings.roots() != m_indexer->settings().roots();
    m_indexer->applySettings(settings);
    if (newFolders)
        follow(); // subscribed to the folders indexed now
    // A first scan this cut short goes on, now with these settings.
    if (m_db.state() != QLatin1String("done") && !m_indexer->isPaused())
        m_indexer->start();
}

void IndexWorker::pause()
{
    if (m_indexer)
        m_indexer->pause();
}

void IndexWorker::resume()
{
    if (!m_indexer)
        return;
    m_indexer->resume();
    if (m_db.state() != QLatin1String("done"))
        m_indexer->start(); // a first scan or a rebuild that a pause cut short
}

void IndexWorker::rebuild()
{
    if (m_indexer)
        m_indexer->rebuild();
}

void IndexWorker::finish()
{
    m_running = nullptr;
    if (m_follower)
        m_follower->stop();
    m_follower.reset();
    m_indexer.reset();
    m_db.close();
}

IndexService::IndexService(QString settingsFile, QString databaseFile, QString socket, QString changesSocket)
    : m_databaseFile(databaseFile)
    , m_socket(std::move(socket))
    , m_thread(new QThread(this))
    , m_worker(new IndexWorker(std::move(settingsFile), std::move(databaseFile), std::move(changesSocket)))
{
    m_thread->setObjectName(QStringLiteral("indexer"));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &IndexWorker::quitAsked, this, &IndexService::quitAsked, Qt::QueuedConnection);
}

IndexService::~IndexService()
{
    shutDown();
}

bool IndexService::start(QString &error)
{
    // One service per user: another one answering on the socket keeps it.
    if (IndexerClient::send(QStringLiteral("ping"), nullptr, 0, m_socket)) {
        error = QStringLiteral("another indexer answers on %1").arg(m_socket);
        return false;
    }
    const QString dir = QFileInfo(m_socket).absolutePath();
    if (!QFileInfo::exists(dir)) {
        QDir().mkpath(QFileInfo(dir).absolutePath());
        ::mkdir(QFile::encodeName(dir).constData(), 0700);
    }
    QFileInfo existing(m_socket);
    if (existing.exists() || existing.isSymLink()) {
        struct stat st;
        // Only a socket left behind is taken over; any other file there is not ours to remove.
        if (::lstat(QFile::encodeName(m_socket).constData(), &st) != 0 || !S_ISSOCK(st.st_mode)) {
            error = QStringLiteral("%1 is not a socket").arg(m_socket);
            return false;
        }
        QLocalServer::removeServer(m_socket);
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_socket)) {
        error = m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &IndexService::answer);
    m_thread->start();
    QMetaObject::invokeMethod(m_worker, &IndexWorker::begin, Qt::QueuedConnection);
    return true;
}

void IndexService::shutDown()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    if (m_thread && m_thread->isRunning()) {
        m_worker->stopNow();
        QMetaObject::invokeMethod(m_worker, &IndexWorker::finish, Qt::QueuedConnection);
        m_thread->quit();
        m_thread->wait();
    }
}

void IndexService::answer()
{
    while (QLocalSocket *client = m_server->nextPendingConnection()) {
        connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        connect(client, &QLocalSocket::readyRead, client, [this, client] {
            // One line per command; anything longer than a command can be is dropped.
            while (client->canReadLine()) {
                const QString line = QString::fromUtf8(client->readLine(256)).trimmed();
                client->write(command(line).toUtf8() + '\n');
            }
            if (client->bytesAvailable() > 256)
                client->abort();
        });
    }
}

QString IndexService::command(const QString &line)
{
    if (line == QLatin1String("ping"))
        return QStringLiteral("pong");
    if (line == QLatin1String("status")) {
        const IndexStatus status = IndexStatus::read(m_databaseFile);
        return QStringLiteral("status %1 %2 %3 %4 %5")
            .arg(status.exists ? status.state : QStringLiteral("empty"),
                 status.paused ? QStringLiteral("paused") : QStringLiteral("running"))
            .arg(status.files)
            .arg(status.entries)
            .arg(status.bytes);
    }
    // A scan running now gives way first, so the command is not kept waiting behind it.
    if (line == QLatin1String("pause")) {
        m_worker->stopNow();
        QMetaObject::invokeMethod(m_worker, &IndexWorker::pause, Qt::QueuedConnection);
    } else if (line == QLatin1String("resume")) {
        QMetaObject::invokeMethod(m_worker, &IndexWorker::resume, Qt::QueuedConnection);
    } else if (line == QLatin1String("rebuild")) {
        m_worker->stopNow();
        QMetaObject::invokeMethod(m_worker, &IndexWorker::rebuild, Qt::QueuedConnection);
    } else if (line == QLatin1String("reload")) {
        m_worker->stopNow();
        QMetaObject::invokeMethod(m_worker, &IndexWorker::reload, Qt::QueuedConnection);
    } else {
        return QStringLiteral("error unknown-command");
    }
    return QStringLiteral("ok");
}

} // namespace Riley
