// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include "forcelayout.h"
#include "graphtree.h"

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace Riley {

// Builds the graph of a folder off the UI thread: walks the tree, then lays it out, on a worker
// thread, and hands both back to the thread that owns the builder. A new start() or stop() drops
// the build before at once; whatever it still makes is thrown away.
class RILEYGRAPH_EXPORT GraphBuilder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY started)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int count READ count NOTIFY built)
    Q_PROPERTY(bool foldersOnly READ foldersOnly NOTIFY built)
    Q_PROPERTY(QString note READ note NOTIFY built)

public:
    explicit GraphBuilder(QObject *parent = nullptr);
    ~GraphBuilder() override;

    // Starts building the graph of folder, dropping the build before.
    Q_INVOKABLE void start(const QString &folder, bool showHidden);
    // Ends the build running; the graph built before stays.
    Q_INVOKABLE void stop();
    // Ends the build running and forgets the graph.
    Q_INVOKABLE void clear();

    QString folder() const { return m_folder; }
    bool isRunning() const { return m_running; }

    // The graph last built, and where each of its nodes is.
    const GraphTree &tree() const { return m_tree; }
    const QList<GraphPoint> &positions() const { return m_positions; }
    int count() const { return m_tree.size(); }
    bool foldersOnly() const { return m_tree.foldersOnly(); }

    // "Showing folders only (20,000 files)." when the tree was too large for its files; empty
    // otherwise.
    QString note() const;
    static QString foldersOnlyNote(qint64 files);

    // What hovering or selecting a node shows: "a · 1 file", "Pictures · 120 files".
    Q_INVOKABLE QString label(int node) const;

    // Moving through the graph: -1 when there is no such node.
    Q_INVOKABLE int parentOf(int node) const;
    Q_INVOKABLE int firstChildOf(int node) const;
    Q_INVOKABLE int nextSiblingOf(int node) const;
    Q_INVOKABLE int previousSiblingOf(int node) const;
    Q_INVOKABLE QString pathOf(int node) const;
    Q_INVOKABLE bool isFolder(int node) const;
    Q_INVOKABLE int indexOf(const QString &path) const { return m_tree.indexOf(path); }

    // Counts each build started; results of an older one are thrown away.
    quint64 generation() const { return m_generation; }

    // Every thread graph work ran on. A test can check that the UI thread is never among them.
    QSet<Qt::HANDLE> threads() const;

signals:
    void started();
    void runningChanged();
    // A build finished and its graph replaced the one before.
    void built();

private:
    void run(quint64 generation, QString folder, bool showHidden, std::shared_ptr<std::atomic<bool>> stop);
    void receive(quint64 generation, GraphTree tree, QList<GraphPoint> positions);
    void setRunning(bool running);
    void noteThread();

    QString m_folder;
    bool m_running = false;
    GraphTree m_tree;
    QList<GraphPoint> m_positions;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_stop;

    mutable QMutex m_threadsLock;
    QSet<Qt::HANDLE> m_threads;

    QThreadPool m_pool;
};

} // namespace Riley
