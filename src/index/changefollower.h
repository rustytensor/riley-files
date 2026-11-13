// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileyindex_export.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QSocketNotifier;
class QTimer;

namespace Riley {

class ChangeClient;
class Indexer;

// The indexer's side of the change helper (riley-files-changes): subscribes to every indexed
// folder, hands each change the helper reports to the Indexer, and applies them in one batch
// every flush interval (2 s). When the helper says changes were lost, or the connection is made
// again after it was gone, the indexer catches up from the folders' modified times instead.
//
// Lives on the indexer's thread, which needs an event loop.
class RILEYINDEX_EXPORT ChangeFollower : public QObject
{
    Q_OBJECT

public:
    explicit ChangeFollower(Indexer &indexer, QString socketPath = {}, QObject *parent = nullptr);
    ~ChangeFollower() override;

    void setFlushInterval(int ms);
    // Connects and subscribes; tries again every few seconds while the helper is not there.
    void start();
    void stop();

    bool isConnected() const;
    // Every indexed folder answered: true once the helper accepted all of them.
    bool isSubscribed() const { return m_subscribed && m_refused.isEmpty() && m_waiting == 0; }
    // The folders the helper refused, with its reason.
    QStringList refused() const { return m_refused; }
    qint64 changesSeen() const { return m_seen; }

signals:
    void subscribed();
    void changesApplied();

private:
    void tryConnect();
    void readHelper();
    void lost();
    void flushNow();

    Indexer &m_indexer;
    QString m_socketPath;
    std::unique_ptr<ChangeClient> m_client;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_flush = nullptr;
    QTimer *m_retry = nullptr;
    bool m_subscribed = false;
    bool m_everConnected = false;
    bool m_catchUpOwed = false;
    int m_waiting = 0;
    QStringList m_refused;
    qint64 m_seen = 0;
};

} // namespace Riley
