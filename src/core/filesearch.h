// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QAbstractListModel>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace Riley {

// One thing a search found: a file or folder whose name matches, or one matching line inside a
// file.
struct SearchHit
{
    QString path;         // absolute
    QString relativePath; // below the folder searched, e.g. "a/b/report.txt"
    bool isDir = false;
    int line = 0;         // contents: the line number, from 1; 0 for a name
    QString text;         // contents: the matching line, trimmed and cut to a readable width
    QList<int> matches;   // contents: start and length pairs of each match within text
};

// Search below a folder, by name or inside files, as rows for a view.
//
// Names: the folder and everything below it is walked on a worker thread. Symbolic links to
// folders are never followed, the walk never leaves the filesystem it started on, and hidden
// files and folders are skipped unless asked for. A name matches when it holds the query
// anywhere, ignoring case. Rows arrive in batches, in path order.
//
// Contents: ripgrep (rg) is run as a child process, from a worker thread, with a list of
// arguments (no shell). Binary files are skipped. Rows are kept in path order, then by line.
//
// A new search, stop() or clear() drops the one before at once: whatever it still finds is
// thrown away, and rg is killed. At most Limit rows are kept; then the search ends and
// capped() is true.
class RILEYCORE_EXPORT FileSearch : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY started)
    Q_PROPERTY(QString query READ query NOTIFY started)
    Q_PROPERTY(Mode mode READ mode NOTIFY started)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool capped READ isCapped NOTIFY noteChanged)
    Q_PROPERTY(QString note READ note NOTIFY noteChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    enum Mode {
        Names,
        Contents,
    };
    Q_ENUM(Mode)

    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,          // absolute path of the file or folder found
        RelativePathRole,  // below the folder searched
        FolderRole,        // absolute path of the folder it is in
        PlaceRole,         // that folder below the folder searched; empty when it is the folder itself
        IsDirRole,
        LineRole,          // contents: line number; 0 for names
        TextRole,          // contents: the matching line
        MatchStartRole,    // contents: where the first match starts in TextRole; -1 for names
        MatchLengthRole,   // contents: its length; 0 for names
        MatchesRole,       // contents: [start, length, start, length, ...]
    };
    Q_ENUM(Role)

    static constexpr int Limit = 2000;
    // The width a matching line is cut to, in characters.
    static constexpr int LineWidth = 300;

    explicit FileSearch(QObject *parent = nullptr);
    ~FileSearch() override;

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Starts a search below folder, dropping the one before. An empty query clears the rows
    // and starts nothing.
    Q_INVOKABLE void start(const QString &folder, const QString &query, Mode mode, bool showHidden);
    // Ends the search running, keeping the rows found so far.
    Q_INVOKABLE void stop();
    // Ends the search running and forgets its rows, its note and its message.
    Q_INVOKABLE void clear();

    QString folder() const { return m_folder; }
    QString query() const { return m_query; }
    Mode mode() const { return m_mode; }
    bool isRunning() const { return m_running; }
    int count() const { return int(m_hits.size()); }
    bool isCapped() const { return m_capped; }
    // "Showing the first 2,000 results." once the limit is reached; empty otherwise.
    QString note() const;
    // Why the search could not run, e.g. "Searching inside files needs ripgrep (rg)."; empty
    // when it could.
    QString message() const { return m_message; }

    const SearchHit &hit(int row) const { return m_hits.at(row); }
    QList<SearchHit> hits() const { return m_hits; }
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString folderAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;

    // Counts each search started; batches from an older one are thrown away.
    quint64 generation() const { return m_generation; }

    // Every thread search work ran on. A test can check that the main thread is never among them.
    QSet<Qt::HANDLE> searchThreads() const;

    static QString missingRipgrepMessage();

signals:
    void started();
    void runningChanged();
    void countChanged();
    void noteChanged();
    void messageChanged();
    // Rows of the search started as `generation` came in.
    void batchAdded(quint64 generation, int count);
    // The search started as `generation` has ended by itself (not by stop, clear or a new one).
    void finished(quint64 generation);

private:
    void runNames(quint64 generation, QString folder, QString query, bool showHidden);
    void runContents(quint64 generation, QString folder, QString query, bool showHidden);
    void post(quint64 generation, QList<SearchHit> hits, bool done, bool capped, QString message);
    void receive(quint64 generation, const QList<SearchHit> &hits, bool done, bool capped,
                 const QString &message);
    void cancel();
    void setRunning(bool running);
    void noteThread();

    QList<SearchHit> m_hits;
    QString m_folder;
    QString m_query;
    Mode m_mode = Names;
    bool m_running = false;
    bool m_capped = false;
    QString m_message;

    quint64 m_generation = 0;
    // The generation still wanted, read by the workers to give up early.
    std::shared_ptr<std::atomic<quint64>> m_wanted;

    mutable QMutex m_threadsLock;
    QSet<Qt::HANDLE> m_threads;

    QThreadPool m_pool;
};

} // namespace Riley
