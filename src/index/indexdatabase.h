// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileyindex_export.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

struct sqlite3;
struct sqlite3_stmt;

namespace Riley {

// One file or folder in the index.
struct RILEYINDEX_EXPORT IndexEntry
{
    qint64 id = 0;
    QString path;      // absolute
    QString name;
    bool folder = false;
    qint64 size = 0;
    qint64 mtime = 0;  // nanoseconds since 1970
    bool contents = false; // its contents are in the contents index

    bool sameRow(const IndexEntry &other) const
    {
        return path == other.path && folder == other.folder && size == other.size && mtime == other.mtime
               && contents == other.contents;
    }
};

// A name a query found, and whether it is a folder's.
struct RILEYINDEX_EXPORT NameMatch
{
    QString path;
    bool folder = false;
};

// A folder read into the index and its modified time when it was read: a restart re-reads only
// the folders whose time changed since.
struct RILEYINDEX_EXPORT FolderRecord
{
    QString path;
    qint64 mtime = 0;
};

// The index file: $XDG_DATA_HOME/riley-files/index.sqlite, mode 0600, in WAL mode so the window
// reads it while the indexer writes. Names go into an FTS5 table with the trigram tokenizer (any
// part of a name, any case); contents into one named bodies, with the unicode61 tokenizer (words).
//
// One object per thread. The window opens it read-only; the indexer (indexer.h) writes through the
// methods in the second half.
class RILEYINDEX_EXPORT IndexDatabase
{
public:
    enum Mode { ReadWrite, ReadOnly };

    IndexDatabase();
    ~IndexDatabase();
    IndexDatabase(const IndexDatabase &) = delete;
    IndexDatabase &operator=(const IndexDatabase &) = delete;

    static QString defaultFile();

    // ReadWrite makes the file (and its folder) when missing. ReadOnly fails when there is none.
    bool open(const QString &file, Mode mode = ReadWrite);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    QString file() const { return m_file; }
    QString errorString() const { return m_error; }

    // ---- Reading ----

    // The paths whose name holds `text` (any case), at or below `below` when it is given, sorted,
    // at most `limit`. Hidden entries (a part of the path below `below` starting with ".") only
    // when `hidden` is set.
    QStringList nameQuery(const QString &text, const QString &below = {}, bool hidden = true, int limit = 2000);
    // The same, with whether each is a folder.
    QList<NameMatch> nameMatches(const QString &text, const QString &below = {}, bool hidden = true, int limit = 2000);
    // The files whose indexed contents hold every word of `text`, below `below` when given.
    QStringList contentsQuery(const QString &text, const QString &below = {}, bool hidden = true, int limit = 2000);
    // The folders whose files' contents are in the index (as the indexer last applied them).
    QStringList contentFolders();
    // Whether the contents index covers `path` (a file or folder below a content folder).
    bool contentsCover(const QString &path);

    qint64 entryCount();
    // The entries that are files, not folders.
    qint64 fileCount();
    qint64 contentCount();
    // The file with its write-ahead log, in bytes.
    qint64 sizeOnDisk() const;
    // "empty", "scanning" or "done", and whether it is paused.
    QString state();
    bool paused();

    QString meta(const QString &key);
    void setMeta(const QString &key, const QString &value);

    // Every entry, sorted by path.
    QList<IndexEntry> entries();
    // Every entry in turn, without holding them all. The table must not change meanwhile.
    void forEachEntry(const std::function<void(const IndexEntry &)> &visit);
    QList<FolderRecord> folderRecords();

    // ---- Writing (the indexer) ----

    bool begin();
    bool commit();
    void rollback();
    // Everything out: entries, names, contents, folder records.
    void clear();

    // The entries directly in `folder`, by name.
    QHash<QString, IndexEntry> children(const QString &folder);
    bool hasFolder(const QString &path);
    qint64 insertEntry(const IndexEntry &entry, const QString &dir);
    void updateEntry(qint64 id, qint64 size, qint64 mtime, bool contents);
    void setContentsFlag(qint64 id, bool contents);
    // `path` and everything below it: entries, contents and folder records.
    void removeTree(const QString &path);
    void setFolderRecord(const QString &path, qint64 mtime);
    void setContents(qint64 id, const QString &text);
    void removeContents(qint64 id);

private:
    sqlite3_stmt *statement(const char *sql);
    bool exec(const char *sql);
    bool createSchema();
    void createTriggers();
    void fail(const QString &what);

    sqlite3 *m_db = nullptr;
    QString m_file;
    QString m_error;
    QHash<const char *, sqlite3_stmt *> m_statements;
};

} // namespace Riley
