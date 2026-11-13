// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "changefollower.h"

#include "changeclient.h"
#include "indexer.h"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>

#include <vector>

namespace Riley {

namespace {
constexpr int kRetryMs = 5000;
constexpr int kFlushMs = 2000;
} // namespace

ChangeFollower::ChangeFollower(Indexer &indexer, QString socketPath, QObject *parent)
    : QObject(parent)
    , m_indexer(indexer)
    , m_socketPath(socketPath.isEmpty() ? QFile::decodeName(ChangeClient::defaultSocket().c_str()) : socketPath)
    , m_client(std::make_unique<ChangeClient>())
    , m_flush(new QTimer(this))
    , m_retry(new QTimer(this))
{
    m_flush->setInterval(kFlushMs);
    connect(m_flush, &QTimer::timeout, this, &ChangeFollower::flushNow);
    m_retry->setInterval(kRetryMs);
    connect(m_retry, &QTimer::timeout, this, &ChangeFollower::tryConnect);
}

ChangeFollower::~ChangeFollower() = default;

void ChangeFollower::setFlushInterval(int ms)
{
    m_flush->setInterval(ms);
}

bool ChangeFollower::isConnected() const
{
    return m_client->isConnected();
}

void ChangeFollower::start()
{
    m_flush->start();
    tryConnect();
}

void ChangeFollower::stop()
{
    m_flush->stop();
    m_retry->stop();
    delete m_notifier;
    m_notifier = nullptr;
    m_client->close();
    m_subscribed = false;
}

void ChangeFollower::tryConnect()
{
    if (m_client->isConnected())
        return;
    if (!m_client->connectTo(QFile::encodeName(m_socketPath).toStdString())) {
        if (!m_retry->isActive())
            m_retry->start();
        return;
    }
    m_retry->stop();
    // Changes made while the helper was not there are found from the folders' times.
    if (m_everConnected)
        m_catchUpOwed = true;
    m_everConnected = true;
    m_refused.clear();
    m_waiting = 0;
    m_subscribed = false;
    for (const QString &root : m_indexer.settings().roots()) {
        if (m_client->subscribe(QFile::encodeName(QDir::cleanPath(root)).toStdString()))
            ++m_waiting;
    }
    m_subscribed = true;
    delete m_notifier;
    m_notifier = new QSocketNotifier(m_client->fd(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &ChangeFollower::readHelper);
    readHelper();
}

void ChangeFollower::readHelper()
{
    std::vector<HelperMessage> messages;
    const bool open = m_client->receive(messages);
    for (const HelperMessage &message : messages) {
        switch (message.type) {
        case HelperMessage::Change:
            ++m_seen;
            m_indexer.noteChange(QFile::decodeName(message.event.path.c_str()));
            break;
        case HelperMessage::Overflow:
            m_catchUpOwed = true;
            break;
        case HelperMessage::Ok:
            if (message.what == "subscribe" && m_waiting > 0 && --m_waiting == 0 && m_refused.isEmpty())
                emit subscribed();
            break;
        case HelperMessage::Error:
            if (!message.path.empty()) {
                if (m_waiting > 0)
                    --m_waiting;
                m_refused.append(QFile::decodeName(message.path.c_str()) + u' ' + QString::fromStdString(message.what));
            }
            break;
        default:
            break;
        }
    }
    if (!open)
        lost();
}

void ChangeFollower::lost()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    m_subscribed = false;
    m_catchUpOwed = true;
    if (!m_retry->isActive())
        m_retry->start();
}

void ChangeFollower::flushNow()
{
    if (m_catchUpOwed && !m_indexer.isPaused()) {
        m_catchUpOwed = false;
        m_indexer.catchUp();
    }
    if (m_indexer.pendingChanges() == 0)
        return;
    m_indexer.flush();
    emit changesApplied();
}

} // namespace Riley
