// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "indexdatabase.h"
#include "indexsettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Riley {

namespace {

constexpr int SchemaVersion = 1;

void bindText(sqlite3_stmt *st, int index, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    sqlite3_bind_text(st, index, bytes.constData(), int(bytes.size()), SQLITE_TRANSIENT);
}

QString columnText(sqlite3_stmt *st, int column)
{
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(st, column));
    return text ? QString::fromUtf8(text, sqlite3_column_bytes(st, column)) : QString();
}

// Every path is absolute, so the paths below `folder` sort between "folder/" and "folder0"
// ("0" follows "/" in byte order). An empty folder means everything.
struct Range
{
    QString after;
    QString before;
};

Range rangeBelow(const QString &folder)
{
    if (folder.isEmpty() || folder == u'/')
        return {QStringLiteral("/"), QStringLiteral("0")};
    return {folder + u'/', folder + u'0'};
}

// A phrase in FTS5's query language: the text in double quotes, quotes inside doubled.
QString phrase(const QString &text)
{
    QString quoted = text;
    quoted.replace(u'"', QLatin1String("\"\""));
    return u'"' + quoted + u'"';
}

QString likePattern(const QString &text)
{
    QString escaped;
    for (const QChar c : text) {
        if (c == u'%' || c == u'_' || c == u'\\')
            escaped += u'\\';
        escaped += c;
    }
    return u'%' + escaped + u'%';
}

IndexEntry entryFrom(sqlite3_stmt *st)
{
    IndexEntry entry;
    entry.id = sqlite3_column_int64(st, 0);
    entry.path = columnText(st, 1);
    entry.name = columnText(st, 2);
    entry.folder = sqlite3_column_int(st, 3) != 0;
    entry.size = sqlite3_column_int64(st, 4);
    entry.mtime = sqlite3_column_int64(st, 5);
    entry.contents = sqlite3_column_int(st, 6) != 0;
    return entry;
}

} // namespace

IndexDatabase::IndexDatabase() = default;

IndexDatabase::~IndexDatabase()
{
    close();
}

QString IndexDatabase::defaultFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/riley-files/index.sqlite");
}

bool IndexDatabase::open(const QString &file, Mode mode)
{
    close();
    m_error.clear();
    m_file = QDir::cleanPath(QFileInfo(file).absoluteFilePath());
    const QByteArray encoded = QFile::encodeName(m_file);

    if (mode == ReadWrite) {
        // The folder only for its owner, and the file made 0600 before SQLite opens it: SQLite
        // gives its log files the same mode.
        const QString dir = QFileInfo(m_file).absolutePath();
        if (!QFileInfo::exists(dir)) {
            QDir().mkpath(QFileInfo(dir).absolutePath());
            ::mkdir(QFile::encodeName(dir).constData(), 0700);
        }
        const int fd = ::open(encoded.constData(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) {
            m_error = QStringLiteral("Cannot create %1: %2").arg(m_file, QString::fromLocal8Bit(strerror(errno)));
            return false;
        }
        ::fchmod(fd, 0600);
        ::close(fd);
    } else if (!QFileInfo::exists(m_file)) {
        m_error = QStringLiteral("There is no index at %1").arg(m_file);
        return false;
    }

    if (sqlite3_open_v2(encoded.constData(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr)
        != SQLITE_OK) {
        fail(QStringLiteral("Cannot open the index"));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(m_db, 5000);
    sqlite3_extended_result_codes(m_db, 1);

    if (mode == ReadOnly) {
        exec("PRAGMA query_only = ON");
        return true;
    }
    if (!exec("PRAGMA journal_mode = WAL") || !exec("PRAGMA synchronous = NORMAL") || !createSchema()) {
        close();
        return false;
    }
    return true;
}

void IndexDatabase::close()
{
    for (sqlite3_stmt *st : std::as_const(m_statements))
        sqlite3_finalize(st);
    m_statements.clear();
    if (m_db)
        sqlite3_close(m_db);
    m_db = nullptr;
}

void IndexDatabase::fail(const QString &what)
{
    m_error = what + QStringLiteral(": ") + QString::fromUtf8(m_db ? sqlite3_errmsg(m_db) : "no database");
}

bool IndexDatabase::exec(const char *sql)
{
    if (!m_db)
        return false;
    char *message = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        m_error = QString::fromUtf8(message ? message : "SQLite error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

sqlite3_stmt *IndexDatabase::statement(const char *sql)
{
    if (!m_db)
        return nullptr;
    sqlite3_stmt *st = m_statements.value(sql);
    if (!st) {
        if (sqlite3_prepare_v3(m_db, sql, -1, SQLITE_PREPARE_PERSISTENT, &st, nullptr) != SQLITE_OK) {
            fail(QStringLiteral("Cannot prepare a query"));
            return nullptr;
        }
        m_statements.insert(sql, st);
    }
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return st;
}

bool IndexDatabase::createSchema()
{
    const bool ok = exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT) WITHOUT ROWID;"
                         "CREATE TABLE IF NOT EXISTS entries("
                         "  id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, dir TEXT NOT NULL,"
                         "  name TEXT NOT NULL, folder INTEGER NOT NULL, size INTEGER NOT NULL,"
                         "  mtime INTEGER NOT NULL, contents INTEGER NOT NULL DEFAULT 0);"
                         "CREATE INDEX IF NOT EXISTS entries_dir ON entries(dir);"
                         "CREATE TABLE IF NOT EXISTS folders(path TEXT PRIMARY KEY, mtime INTEGER NOT NULL)"
                         "  WITHOUT ROWID;"
                         "CREATE VIRTUAL TABLE IF NOT EXISTS names USING fts5("
                         "  name, content='entries', content_rowid='id', tokenize='trigram');"
                         "CREATE VIRTUAL TABLE IF NOT EXISTS bodies USING fts5("
                         "  body, content='', contentless_delete=1, tokenize='unicode61');");
    if (!ok)
        return false;
    createTriggers();
    if (meta(QStringLiteral("schema")).isEmpty())
        setMeta(QStringLiteral("schema"), QString::number(SchemaVersion));
    return true;
}

void IndexDatabase::createTriggers()
{
    exec("CREATE TRIGGER IF NOT EXISTS entries_added AFTER INSERT ON entries BEGIN"
         "  INSERT INTO names(rowid, name) VALUES (new.id, new.name);"
         "END;"
         "CREATE TRIGGER IF NOT EXISTS entries_removed AFTER DELETE ON entries BEGIN"
         "  INSERT INTO names(names, rowid, name) VALUES ('delete', old.id, old.name);"
         "  DELETE FROM bodies WHERE rowid = old.id;"
         "END;"
         "CREATE TRIGGER IF NOT EXISTS entries_renamed AFTER UPDATE OF name ON entries BEGIN"
         "  INSERT INTO names(names, rowid, name) VALUES ('delete', old.id, old.name);"
         "  INSERT INTO names(rowid, name) VALUES (new.id, new.name);"
         "END;");
}

// ---- Reading ----

QStringList IndexDatabase::nameQuery(const QString &text, const QString &below, bool hidden, int limit)
{
    QStringList paths;
    const QList<NameMatch> found = nameMatches(text, below, hidden, limit);
    paths.reserve(found.size());
    for (const NameMatch &match : found)
        paths.append(match.path);
    return paths;
}

QList<NameMatch> IndexDatabase::nameMatches(const QString &text, const QString &below, bool hidden, int limit)
{
    QList<NameMatch> found;
    const QString needle = text.trimmed();
    if (needle.isEmpty() || limit <= 0)
        return found;
    const Range range = rangeBelow(below);
    // The trigram index answers for three characters or more; shorter ones are looked for in
    // every name.
    sqlite3_stmt *st = needle.size() >= 3
                           ? statement("SELECT e.path, e.folder FROM names JOIN entries e ON e.id = names.rowid"
                                       " WHERE names MATCH ?1 AND e.path > ?2 AND e.path < ?3"
                                       " AND (?4 OR instr(substr(e.path, length(?5) + 1), '/.') = 0)"
                                       " ORDER BY e.path LIMIT ?6")
                           : statement("SELECT e.path, e.folder FROM entries e"
                                       " WHERE e.name LIKE ?1 ESCAPE '\\' AND e.path > ?2 AND e.path < ?3"
                                       " AND (?4 OR instr(substr(e.path, length(?5) + 1), '/.') = 0)"
                                       " ORDER BY e.path LIMIT ?6");
    if (!st)
        return found;
    bindText(st, 1, needle.size() >= 3 ? phrase(needle) : likePattern(needle));
    bindText(st, 2, range.after);
    bindText(st, 3, range.before);
    sqlite3_bind_int(st, 4, hidden ? 1 : 0);
    bindText(st, 5, below);
    sqlite3_bind_int(st, 6, limit);
    while (sqlite3_step(st) == SQLITE_ROW)
        found.append({columnText(st, 0), sqlite3_column_int(st, 1) != 0});
    sqlite3_reset(st);
    return found;
}

QStringList IndexDatabase::contentsQuery(const QString &text, const QString &below, bool hidden, int limit)
{
    QStringList paths;
    QStringList words;
    for (const QString &word : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
        words.append(phrase(word));
    if (words.isEmpty() || limit <= 0)
        return paths;
    const Range range = rangeBelow(below);
    sqlite3_stmt *st = statement("SELECT e.path FROM bodies JOIN entries e ON e.id = bodies.rowid"
                                 " WHERE bodies MATCH ?1 AND e.path > ?2 AND e.path < ?3"
                                 " AND (?4 OR instr(substr(e.path, length(?5) + 1), '/.') = 0)"
                                 " ORDER BY e.path LIMIT ?6");
    if (!st)
        return paths;
    bindText(st, 1, words.join(u' '));
    bindText(st, 2, range.after);
    bindText(st, 3, range.before);
    sqlite3_bind_int(st, 4, hidden ? 1 : 0);
    bindText(st, 5, below);
    sqlite3_bind_int(st, 6, limit);
    while (sqlite3_step(st) == SQLITE_ROW)
        paths.append(columnText(st, 0));
    sqlite3_reset(st);
    return paths;
}

QStringList IndexDatabase::contentFolders()
{
    return meta(QStringLiteral("contentFolders")).split(u'\n', Qt::SkipEmptyParts);
}

bool IndexDatabase::contentsCover(const QString &path)
{
    if (state() != QLatin1String("done"))
        return false;
    const QString clean = QDir::cleanPath(path);
    const QStringList folders = contentFolders();
    return std::any_of(folders.begin(), folders.end(), [&](const QString &f) { return isAtOrBelow(clean, f); });
}

qint64 IndexDatabase::entryCount()
{
    sqlite3_stmt *st = statement("SELECT count(*) FROM entries");
    qint64 count = st && sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    if (st)
        sqlite3_reset(st);
    return count;
}

qint64 IndexDatabase::fileCount()
{
    sqlite3_stmt *st = statement("SELECT count(*) FROM entries WHERE folder = 0");
    qint64 count = st && sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    if (st)
        sqlite3_reset(st);
    return count;
}

qint64 IndexDatabase::contentCount()
{
    sqlite3_stmt *st = statement("SELECT count(*) FROM entries WHERE contents = 1");
    qint64 count = st && sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    if (st)
        sqlite3_reset(st);
    return count;
}

qint64 IndexDatabase::sizeOnDisk() const
{
    return QFileInfo(m_file).size() + QFileInfo(m_file + QStringLiteral("-wal")).size();
}

QString IndexDatabase::state()
{
    const QString value = meta(QStringLiteral("state"));
    return value.isEmpty() ? QStringLiteral("empty") : value;
}

bool IndexDatabase::paused()
{
    return meta(QStringLiteral("paused")) == QLatin1String("true");
}

QString IndexDatabase::meta(const QString &key)
{
    sqlite3_stmt *st = statement("SELECT value FROM meta WHERE key = ?1");
    if (!st)
        return {};
    bindText(st, 1, key);
    const QString value = sqlite3_step(st) == SQLITE_ROW ? columnText(st, 0) : QString();
    sqlite3_reset(st);
    return value;
}

void IndexDatabase::setMeta(const QString &key, const QString &value)
{
    sqlite3_stmt *st = statement("INSERT OR REPLACE INTO meta(key, value) VALUES (?1, ?2)");
    if (!st)
        return;
    bindText(st, 1, key);
    bindText(st, 2, value);
    sqlite3_step(st);
    sqlite3_reset(st);
}

QList<IndexEntry> IndexDatabase::entries()
{
    QList<IndexEntry> list;
    sqlite3_stmt *st = statement("SELECT id, path, name, folder, size, mtime, contents FROM entries ORDER BY path");
    if (!st)
        return list;
    while (sqlite3_step(st) == SQLITE_ROW)
        list.append(entryFrom(st));
    sqlite3_reset(st);
    return list;
}

void IndexDatabase::forEachEntry(const std::function<void(const IndexEntry &)> &visit)
{
    // Its own statement, not the cached one, so `visit` may run other queries.
    sqlite3_stmt *st = nullptr;
    if (!m_db
        || sqlite3_prepare_v2(m_db, "SELECT id, path, name, folder, size, mtime, contents FROM entries", -1, &st,
                              nullptr)
               != SQLITE_OK)
        return;
    while (sqlite3_step(st) == SQLITE_ROW)
        visit(entryFrom(st));
    sqlite3_finalize(st);
}

QList<FolderRecord> IndexDatabase::folderRecords()
{
    QList<FolderRecord> list;
    sqlite3_stmt *st = statement("SELECT path, mtime FROM folders ORDER BY path");
    if (!st)
        return list;
    while (sqlite3_step(st) == SQLITE_ROW)
        list.append({columnText(st, 0), sqlite3_column_int64(st, 1)});
    sqlite3_reset(st);
    return list;
}

// ---- Writing ----

bool IndexDatabase::begin()
{
    return exec("BEGIN IMMEDIATE");
}

bool IndexDatabase::commit()
{
    return exec("COMMIT");
}

void IndexDatabase::rollback()
{
    exec("ROLLBACK");
}

void IndexDatabase::clear()
{
    // Row by row through the triggers would take long on a large index: the triggers are
    // dropped, both text indexes emptied at once, and the triggers made again.
    exec("DROP TRIGGER IF EXISTS entries_added;"
         "DROP TRIGGER IF EXISTS entries_removed;"
         "DROP TRIGGER IF EXISTS entries_renamed;"
         "DELETE FROM entries;"
         "DELETE FROM folders;"
         "INSERT INTO names(names) VALUES ('delete-all');"
         "INSERT INTO bodies(bodies) VALUES ('delete-all');");
    createTriggers();
}

QHash<QString, IndexEntry> IndexDatabase::children(const QString &folder)
{
    QHash<QString, IndexEntry> map;
    sqlite3_stmt *st = statement("SELECT id, path, name, folder, size, mtime, contents FROM entries WHERE dir = ?1");
    if (!st)
        return map;
    bindText(st, 1, folder);
    while (sqlite3_step(st) == SQLITE_ROW) {
        IndexEntry entry = entryFrom(st);
        map.insert(entry.name, entry);
    }
    sqlite3_reset(st);
    return map;
}

bool IndexDatabase::hasFolder(const QString &path)
{
    sqlite3_stmt *st = statement("SELECT 1 FROM folders WHERE path = ?1");
    if (!st)
        return false;
    bindText(st, 1, path);
    const bool found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_reset(st);
    return found;
}

qint64 IndexDatabase::insertEntry(const IndexEntry &entry, const QString &dir)
{
    sqlite3_stmt *st = statement("INSERT INTO entries(path, dir, name, folder, size, mtime, contents)"
                                 " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    if (!st)
        return 0;
    bindText(st, 1, entry.path);
    bindText(st, 2, dir);
    bindText(st, 3, entry.name);
    sqlite3_bind_int(st, 4, entry.folder ? 1 : 0);
    sqlite3_bind_int64(st, 5, entry.size);
    sqlite3_bind_int64(st, 6, entry.mtime);
    sqlite3_bind_int(st, 7, entry.contents ? 1 : 0);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_reset(st);
    if (!ok) {
        fail(QStringLiteral("Cannot add %1").arg(entry.path));
        return 0;
    }
    return sqlite3_last_insert_rowid(m_db);
}

void IndexDatabase::updateEntry(qint64 id, qint64 size, qint64 mtime, bool contents)
{
    sqlite3_stmt *st = statement("UPDATE entries SET size = ?2, mtime = ?3, contents = ?4 WHERE id = ?1");
    if (!st)
        return;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int64(st, 2, size);
    sqlite3_bind_int64(st, 3, mtime);
    sqlite3_bind_int(st, 4, contents ? 1 : 0);
    sqlite3_step(st);
    sqlite3_reset(st);
}

void IndexDatabase::setContentsFlag(qint64 id, bool contents)
{
    sqlite3_stmt *st = statement("UPDATE entries SET contents = ?2 WHERE id = ?1");
    if (!st)
        return;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int(st, 2, contents ? 1 : 0);
    sqlite3_step(st);
    sqlite3_reset(st);
}

void IndexDatabase::removeTree(const QString &path)
{
    const Range range = rangeBelow(path);
    for (const char *sql : {"DELETE FROM entries WHERE path = ?1 OR (path > ?2 AND path < ?3)",
                            "DELETE FROM folders WHERE path = ?1 OR (path > ?2 AND path < ?3)"}) {
        sqlite3_stmt *st = statement(sql);
        if (!st)
            return;
        bindText(st, 1, path);
        bindText(st, 2, range.after);
        bindText(st, 3, range.before);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
}

void IndexDatabase::setFolderRecord(const QString &path, qint64 mtime)
{
    sqlite3_stmt *st = statement("INSERT OR REPLACE INTO folders(path, mtime) VALUES (?1, ?2)");
    if (!st)
        return;
    bindText(st, 1, path);
    sqlite3_bind_int64(st, 2, mtime);
    sqlite3_step(st);
    sqlite3_reset(st);
}

void IndexDatabase::setContents(qint64 id, const QString &text)
{
    removeContents(id);
    sqlite3_stmt *st = statement("INSERT INTO bodies(rowid, body) VALUES (?1, ?2)");
    if (!st)
        return;
    sqlite3_bind_int64(st, 1, id);
    bindText(st, 2, text);
    sqlite3_step(st);
    sqlite3_reset(st);
}

void IndexDatabase::removeContents(qint64 id)
{
    sqlite3_stmt *st = statement("DELETE FROM bodies WHERE rowid = ?1");
    if (!st)
        return;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_step(st);
    sqlite3_reset(st);
}

} // namespace Riley
