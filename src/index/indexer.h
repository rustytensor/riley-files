// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "indexdatabase.h"
#include "indexsettings.h"
#include "rileyindex_export.h"

#include <QHash>
#include <QSet>
#include <QString>

#include <atomic>
#include <sys/types.h>

namespace Riley {

// Keeps the index matching the disk. The first scan reads every chosen folder (never following a
// link to a folder, never crossing into another file system unless that folder is listed too);
// afterwards only changes are applied: the paths the change helper reports, re-read folder by
// folder, and after a restart the folders whose modified time changed while the indexer was not
// running.
//
// It works on the calling thread and does not return until the work is done; the indexer service
// runs it on a thread of its own. requestStop() from another thread ends a long scan between two
// folders, and the next start() picks up from there.
class RILEYINDEX_EXPORT Indexer
{
public:
    // What the last call read, so it can be checked that a catch-up read little.
    struct Stats
    {
        qint64 foldersRead = 0;    // folders listed
        qint64 foldersChecked = 0; // folder records compared with the disk
        qint64 contentsRead = 0;   // files whose contents were read
    };

    Indexer(IndexDatabase &db, const IndexSettings &settings);

    const IndexSettings &settings() const { return m_settings; }

    // A first scan when the index was never completed, otherwise a catch-up. The settings the
    // index was last made with are applied first when they differ from these.
    void start();
    // Everything again from nothing: the first scan, and Rebuild.
    void fullScan();
    void rebuild() { fullScan(); }
    // Re-reads only the folders whose modified time changed since they were read.
    void catchUp();
    // New settings, applied to what is indexed: excluded entries out, new folders read, removed
    // folders out, contents in or out where they changed.
    void applySettings(const IndexSettings &settings);

    // Something changed at `path` (made, removed, renamed to or from, written): kept until flush().
    void noteChange(const QString &path);
    qsizetype pendingChanges() const { return m_pending.size(); }
    // Applies the changes kept so far, unless paused.
    void flush();

    // Paused, changes are kept and not applied; Resume applies them. Kept in the index, so a
    // restart stays paused.
    void pause();
    void resume();
    bool isPaused() const { return m_paused; }

    // "empty", "scanning", "done" or "paused".
    QString state();
    Stats lastRun() const { return m_stats; }

    void requestStop() { m_stop = true; }

private:
    struct Work
    {
        QString path;
        dev_t device = 0;
        bool deep = false;
    };

    void useSettings(const IndexSettings &settings);
    void applyChanges(const IndexSettings &from, const IndexSettings &to);
    void scanTree(const QString &root, bool deep);
    void readFolder(const Work &work, QList<Work> &more);
    void updateContents(qint64 id, const QString &path, bool wanted, bool had, qint64 size);
    bool readText(const QString &path, QString &text);
    bool contentsWanted(const QString &path) const;
    bool coveredByRoot(const QString &path) const;
    bool pathExcluded(const QString &path) const;
    QString rootOf(const QString &path) const;
    dev_t deviceOf(const QString &path) const;
    void storeApplied();
    IndexSettings appliedSettings();
    void batchPoint();

    IndexDatabase &m_db;
    IndexSettings m_settings;
    QStringList m_roots;        // every listed folder, cleaned
    QStringList m_scanRoots;    // those not below another one
    QStringList m_contentRoots; // the content folders, cleaned
    QSet<QString> m_pending;
    bool m_paused = false;
    bool m_catchUpOwed = false;
    int m_writes = 0;
    Stats m_stats;
    std::atomic<bool> m_stop = false;
};

} // namespace Riley
