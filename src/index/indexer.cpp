// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "indexer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace Riley {

namespace {

// Writes per transaction during a scan: the window's queries see the work as it goes, and the
// write-ahead log stays small.
constexpr int WritesPerBatch = 5000;

qint64 nanoseconds(const struct statx_timestamp &t)
{
    return qint64(t.tv_sec) * 1000000000 + t.tv_nsec;
}

QString childPath(const QString &folder, const QString &name)
{
    return folder == u'/' ? u'/' + name : folder + u'/' + name;
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList cleanAll(const QStringList &paths)
{
    QStringList out;
    for (const QString &path : paths)
        if (!path.trimmed().isEmpty())
            out.append(cleanPath(path.trimmed()));
    out.removeDuplicates();
    std::sort(out.begin(), out.end());
    return out;
}

bool statPath(const QString &path, bool follow, struct statx &st)
{
    const int flags = (follow ? 0 : AT_SYMLINK_NOFOLLOW) | AT_NO_AUTOMOUNT | AT_STATX_SYNC_AS_STAT;
    return ::statx(AT_FDCWD, QFile::encodeName(path).constData(), flags,
                   STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_MTIME, &st)
           == 0;
}

} // namespace

Indexer::Indexer(IndexDatabase &db, const IndexSettings &settings)
    : m_db(db)
{
    useSettings(settings);
    m_paused = m_db.paused();
}

void Indexer::useSettings(const IndexSettings &settings)
{
    m_settings = settings;
    m_roots = cleanAll(settings.folders);
    m_scanRoots.clear();
    for (const QString &root : std::as_const(m_roots)) {
        const bool below = std::any_of(m_scanRoots.begin(), m_scanRoots.end(),
                                       [&](const QString &outer) { return isAtOrBelow(root, outer); });
        if (!below)
            m_scanRoots.append(root);
    }
    m_contentRoots = cleanAll(settings.contentFolders);
}

// ---- Settings remembered in the index ----

void Indexer::storeApplied()
{
    m_db.setMeta(QStringLiteral("applied.folders"), m_roots.join(u'\n'));
    m_db.setMeta(QStringLiteral("applied.excludes"), m_settings.excludes.join(u'\n'));
    m_db.setMeta(QStringLiteral("applied.contentFolders"), m_contentRoots.join(u'\n'));
    m_db.setMeta(QStringLiteral("applied.contentLimit"), QString::number(m_settings.contentLimit));
    m_db.setMeta(QStringLiteral("applied.textOnly"), m_settings.textOnly ? QStringLiteral("true") : QStringLiteral("false"));
    // What the window reads to know where the index answers for contents.
    m_db.setMeta(QStringLiteral("contentFolders"), m_contentRoots.join(u'\n'));
}

IndexSettings Indexer::appliedSettings()
{
    IndexSettings applied;
    applied.enabled = true;
    applied.folders = m_db.meta(QStringLiteral("applied.folders")).split(u'\n', Qt::SkipEmptyParts);
    applied.excludes = m_db.meta(QStringLiteral("applied.excludes")).split(u'\n', Qt::SkipEmptyParts);
    applied.contentFolders = m_db.meta(QStringLiteral("applied.contentFolders")).split(u'\n', Qt::SkipEmptyParts);
    applied.contentLimit = m_db.meta(QStringLiteral("applied.contentLimit")).toLongLong();
    applied.textOnly = m_db.meta(QStringLiteral("applied.textOnly")) != QLatin1String("false");
    return applied;
}

// ---- Paths ----

bool Indexer::coveredByRoot(const QString &path) const
{
    return std::any_of(m_scanRoots.begin(), m_scanRoots.end(), [&](const QString &root) { return isAtOrBelow(path, root); });
}

QString Indexer::rootOf(const QString &path) const
{
    QString best;
    for (const QString &root : m_roots)
        if (isAtOrBelow(path, root) && root.size() > best.size())
            best = root;
    return best;
}

// Whether a part of `path` below its root is left out by the exclude patterns.
bool Indexer::pathExcluded(const QString &path) const
{
    const QString root = rootOf(path);
    if (root.isEmpty())
        return true;
    QString walk = path;
    while (walk.size() > root.size()) {
        if (m_settings.isExcluded(walk.mid(walk.lastIndexOf(u'/') + 1), walk))
            return true;
        walk = parentOf(walk);
    }
    return false;
}

dev_t Indexer::deviceOf(const QString &path) const
{
    struct statx st;
    if (!statPath(path, true, st))
        return 0;
    return makedev(st.stx_dev_major, st.stx_dev_minor);
}

bool Indexer::contentsWanted(const QString &path) const
{
    return std::any_of(m_contentRoots.begin(), m_contentRoots.end(),
                       [&](const QString &folder) { return isAtOrBelow(path, folder); });
}

// ---- Reading folders ----

void Indexer::batchPoint()
{
    if (m_writes < WritesPerBatch)
        return;
    m_db.commit();
    m_db.begin();
    m_writes = 0;
}

void Indexer::scanTree(const QString &root, bool deep)
{
    struct statx st;
    if (!statPath(root, true, st) || !S_ISDIR(st.stx_mode)) {
        m_db.removeTree(root);
        return;
    }
    QList<Work> stack{{root, makedev(st.stx_dev_major, st.stx_dev_minor), deep}};
    while (!stack.isEmpty() && !m_stop) {
        const Work work = stack.takeLast();
        readFolder(work, stack);
        batchPoint();
    }
}

// Lists one folder and makes the index's entries for it match: new ones added (new folders
// queued to be read whole), changed ones updated, gone ones removed with everything below them.
// Folders already known are queued only for a deep read.
void Indexer::readFolder(const Work &work, QList<Work> &more)
{
    const QString &folder = work.path;
    const bool isRoot = m_roots.contains(folder);
    const int fd = ::open(QFile::encodeName(folder).constData(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | (isRoot ? 0 : O_NOFOLLOW));
    if (fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR || errno == ELOOP) {
            m_db.removeTree(folder);
            ++m_writes;
            return;
        }
        // Unreadable: what was known below it is no longer known.
        const auto known = m_db.children(folder);
        for (const IndexEntry &entry : known)
            m_db.removeTree(entry.path);
        struct statx st;
        m_db.setFolderRecord(folder, statPath(folder, isRoot, st) ? nanoseconds(st.stx_mtime) : 0);
        m_writes += int(known.size()) + 1;
        return;
    }
    ++m_stats.foldersRead;

    // The folder's time is taken before it is listed: a change made while listing shows as a
    // newer time at the next catch-up.
    struct statx self;
    const qint64 folderTime = ::statx(fd, "", AT_EMPTY_PATH, STATX_MTIME, &self) == 0 ? nanoseconds(self.stx_mtime) : 0;

    DIR *dir = ::fdopendir(fd);
    if (!dir) {
        ::close(fd);
        return;
    }
    QHash<QString, IndexEntry> known = m_db.children(folder);
    while (const dirent *ent = ::readdir(dir)) {
        const char *raw = ent->d_name;
        if (raw[0] == '.' && (raw[1] == 0 || (raw[1] == '.' && raw[2] == 0)))
            continue;
        const QString name = QFile::decodeName(raw);
        if (QFile::encodeName(name) != QByteArrayView(raw))
            continue; // a name that is not in the system's encoding cannot be searched for
        const QString path = childPath(folder, name);
        if (m_settings.isExcluded(name, path))
            continue; // when it was known, it is removed below

        struct statx st;
        if (::statx(::dirfd(dir), raw, AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT | AT_STATX_SYNC_AS_STAT,
                    STATX_TYPE | STATX_MODE | STATX_SIZE | STATX_MTIME, &st)
            != 0)
            continue;
        const bool isDir = S_ISDIR(st.stx_mode);
        const bool isFile = S_ISREG(st.stx_mode);
        const qint64 size = isDir ? 0 : qint64(st.stx_size);
        const qint64 mtime = nanoseconds(st.stx_mtime);

        auto it = known.find(name);
        bool isNew = it == known.end();
        if (!isNew && it->folder != isDir) {
            m_db.removeTree(path);
            ++m_writes;
            isNew = true;
        }
        if (isNew) {
            IndexEntry entry;
            entry.path = path;
            entry.name = name;
            entry.folder = isDir;
            entry.size = size;
            entry.mtime = mtime;
            const qint64 id = m_db.insertEntry(entry, folder);
            ++m_writes;
            if (isFile && id)
                updateContents(id, path, contentsWanted(path), false, size);
        } else {
            if (it->size != size || it->mtime != mtime) {
                m_db.updateEntry(it->id, size, mtime, it->contents);
                ++m_writes;
                if (isFile && (it->contents || contentsWanted(path)))
                    updateContents(it->id, path, contentsWanted(path), it->contents, size);
            }
            known.erase(it);
        }
        if (isDir) {
            const dev_t device = makedev(st.stx_dev_major, st.stx_dev_minor);
            const bool listed = m_roots.contains(path);
            if ((device == work.device || listed) && (work.deep || isNew))
                more.append({path, device, true});
        }
    }
    ::closedir(dir);

    for (const IndexEntry &gone : std::as_const(known)) {
        m_db.removeTree(gone.path);
        ++m_writes;
    }
    m_db.setFolderRecord(folder, folderTime);
    ++m_writes;
}

void Indexer::updateContents(qint64 id, const QString &path, bool wanted, bool had, qint64 size)
{
    QString text;
    if (wanted && size <= m_settings.contentLimit && readText(path, text)) {
        m_db.setContents(id, text);
        m_db.setContentsFlag(id, true);
        ++m_stats.contentsRead;
        m_writes += 2;
    } else if (had) {
        m_db.removeContents(id);
        m_db.setContentsFlag(id, false);
        m_writes += 2;
    }
}

bool Indexer::readText(const QString &path, QString &text)
{
    const QByteArray encoded = QFile::encodeName(path);
    int fd = ::open(encoded.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NOATIME | O_NONBLOCK);
    if (fd < 0 && errno == EPERM)
        fd = ::open(encoded.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return false;
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > m_settings.contentLimit) {
        ::close(fd);
        return false;
    }
    QByteArray bytes;
    bytes.resize(qsizetype(std::min<qint64>(m_settings.contentLimit, st.st_size)) + 1);
    qsizetype got = 0;
    while (got < bytes.size()) {
        const ssize_t n = ::read(fd, bytes.data() + got, size_t(bytes.size() - got));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        got += n;
    }
    ::close(fd);
    if (got > m_settings.contentLimit)
        return false; // grew past the limit since it was looked at
    bytes.truncate(got);

    if (m_settings.textOnly && std::memchr(bytes.constData(), 0, size_t(bytes.size())))
        return false;
    QStringDecoder decoder(QStringDecoder::Utf8);
    text = decoder.decode(bytes);
    if (m_settings.textOnly && decoder.hasError())
        return false;
    text.remove(QChar(0));
    return true;
}

// ---- Work ----

void Indexer::start()
{
    m_stats = {};
    if (m_db.state() != QLatin1String("done")) {
        fullScan();
        return;
    }
    const IndexSettings applied = appliedSettings();
    Stats settingsWork;
    if (!(applied.folders == m_roots && applied.excludes == m_settings.excludes
          && applied.contentFolders == m_contentRoots && applied.contentLimit == m_settings.contentLimit
          && applied.textOnly == m_settings.textOnly)) {
        m_db.begin();
        applyChanges(applied, m_settings);
        m_db.commit();
        settingsWork = m_stats;
    }
    if (m_paused) {
        m_catchUpOwed = true;
        return;
    }
    catchUp();
    m_stats.foldersRead += settingsWork.foldersRead;
    m_stats.contentsRead += settingsWork.contentsRead;
}

void Indexer::fullScan()
{
    m_stats = {};
    m_stop = false;
    m_pending.clear();
    m_db.begin();
    m_db.clear();
    m_db.setMeta(QStringLiteral("state"), QStringLiteral("scanning"));
    storeApplied();
    m_db.commit();

    m_db.begin();
    m_writes = 0;
    for (const QString &root : std::as_const(m_scanRoots)) {
        if (m_stop)
            break;
        scanTree(root, true);
    }
    if (!m_stop)
        m_db.setMeta(QStringLiteral("state"), QStringLiteral("done"));
    m_db.commit();
}

void Indexer::catchUp()
{
    m_stats = {};
    m_stop = false;
    m_catchUpOwed = false;
    if (m_db.state() != QLatin1String("done")) {
        fullScan();
        return;
    }
    m_db.begin();
    m_writes = 0;
    const QList<FolderRecord> records = m_db.folderRecords();
    QSet<QString> recorded;
    QHash<QString, dev_t> devices;
    for (const FolderRecord &record : records) {
        if (m_stop)
            break;
        recorded.insert(record.path);
        ++m_stats.foldersChecked;
        if (!coveredByRoot(record.path)) {
            m_db.removeTree(record.path);
            continue;
        }
        const bool isRoot = m_roots.contains(record.path);
        struct statx st;
        if (!statPath(record.path, isRoot, st) || !S_ISDIR(st.stx_mode)) {
            // Gone: its parent's time changed too, and reading the parent removes it; a root
            // has no parent in the index.
            if (isRoot)
                m_db.removeTree(record.path);
            continue;
        }
        if (nanoseconds(st.stx_mtime) == record.mtime)
            continue;
        const QString root = rootOf(record.path);
        if (!devices.contains(root))
            devices.insert(root, deviceOf(root));
        const dev_t device = m_roots.contains(record.path) ? makedev(st.stx_dev_major, st.stx_dev_minor)
                                                           : devices.value(root);
        QList<Work> more;
        readFolder({record.path, device, false}, more);
        while (!more.isEmpty() && !m_stop) {
            const Work work = more.takeLast();
            readFolder(work, more);
            batchPoint();
        }
        batchPoint();
    }
    for (const QString &root : std::as_const(m_scanRoots))
        if (!recorded.contains(root) && !m_stop)
            scanTree(root, true);
    m_db.commit();
}

void Indexer::applySettings(const IndexSettings &settings)
{
    m_stats = {};
    m_stop = false;
    const IndexSettings from = m_db.state() == QLatin1String("done") ? appliedSettings() : m_settings;
    useSettings(settings);
    if (m_db.state() != QLatin1String("done")) {
        m_db.begin();
        storeApplied();
        m_db.commit();
        return;
    }
    m_db.begin();
    applyChanges(from, settings);
    m_db.commit();
}

void Indexer::applyChanges(const IndexSettings &from, const IndexSettings &to)
{
    m_writes = 0;
    const QStringList oldRoots = cleanAll(from.folders);
    const QStringList oldContent = cleanAll(from.contentFolders);

    // Folders no longer indexed.
    for (const QString &old : oldRoots) {
        if (!coveredByRoot(old)) {
            m_db.removeTree(old);
            ++m_writes;
        }
    }

    // Entries a new pattern leaves out.
    const bool patternAdded = std::any_of(to.excludes.begin(), to.excludes.end(),
                                          [&](const QString &p) { return !from.excludes.contains(p); });
    const bool patternRemoved = std::any_of(from.excludes.begin(), from.excludes.end(),
                                            [&](const QString &p) { return !to.excludes.contains(p); });
    // Contents to look at again.
    const bool contentsChanged = oldContent != m_contentRoots || from.contentLimit != to.contentLimit
                                 || from.textOnly != to.textOnly;
    const auto wantedBefore = [&](const QString &path) {
        return std::any_of(oldContent.begin(), oldContent.end(), [&](const QString &f) { return isAtOrBelow(path, f); });
    };

    QStringList excluded;
    QList<IndexEntry> recheck;
    if (patternAdded || contentsChanged) {
        m_db.forEachEntry([&](const IndexEntry &entry) {
            if (patternAdded && m_settings.isExcluded(entry.name, entry.path)) {
                excluded.append(entry.path);
                return;
            }
            if (!contentsChanged || entry.folder)
                return;
            const bool wanted = contentsWanted(entry.path);
            if (entry.contents) {
                // Still right only when wanted, within the limit and read the same way.
                if (!wanted || entry.size > to.contentLimit || from.textOnly != to.textOnly)
                    recheck.append(entry);
            } else if (wanted) {
                // Looked at before and refused, under the same rules: nothing new to learn.
                const bool refusedAlike = wantedBefore(entry.path) && from.textOnly == to.textOnly
                                          && (entry.size <= from.contentLimit || entry.size > to.contentLimit);
                if (!refusedAlike)
                    recheck.append(entry);
            }
        });
    }
    for (const QString &path : std::as_const(excluded)) {
        m_db.removeTree(path);
        ++m_writes;
        batchPoint();
    }
    for (const IndexEntry &entry : std::as_const(recheck)) {
        struct statx st;
        if (!statPath(entry.path, false, st) || !S_ISREG(st.stx_mode))
            continue; // a change the next read of its folder takes in
        updateContents(entry.id, entry.path, contentsWanted(entry.path), entry.contents, qint64(st.stx_size));
        batchPoint();
    }

    // New folders read whole; with a pattern gone, everything read again for what it held back.
    for (const QString &root : std::as_const(m_scanRoots)) {
        const bool known = std::any_of(oldRoots.begin(), oldRoots.end(), [&](const QString &old) { return isAtOrBelow(root, old); });
        if (!known || patternRemoved)
            scanTree(root, true);
    }
    storeApplied();
}

void Indexer::noteChange(const QString &path)
{
    if (!path.isEmpty())
        m_pending.insert(cleanPath(path));
}

void Indexer::flush()
{
    m_stats = {};
    if (m_paused || m_pending.isEmpty())
        return;
    m_stop = false;
    const QSet<QString> changed = std::exchange(m_pending, {});

    // Each change is applied by reading again the nearest folder the index knows that holds it.
    QSet<QString> folders;
    for (const QString &path : changed) {
        if (!coveredByRoot(path) || pathExcluded(path))
            continue;
        if (m_roots.contains(path)) {
            folders.insert(path);
            continue;
        }
        QString folder = parentOf(path);
        while (!m_db.hasFolder(folder) && !m_roots.contains(folder) && coveredByRoot(parentOf(folder)))
            folder = parentOf(folder);
        folders.insert(folder);
    }
    if (folders.isEmpty())
        return;

    QStringList sorted(folders.begin(), folders.end());
    std::sort(sorted.begin(), sorted.end());
    m_db.begin();
    m_writes = 0;
    QHash<QString, dev_t> devices;
    for (const QString &folder : std::as_const(sorted)) {
        const QString root = rootOf(folder);
        if (!devices.contains(root))
            devices.insert(root, deviceOf(root));
        const dev_t device = m_roots.contains(folder) ? deviceOf(folder) : devices.value(root);
        QList<Work> more;
        readFolder({folder, device, false}, more);
        while (!more.isEmpty() && !m_stop) {
            const Work work = more.takeLast();
            readFolder(work, more);
            batchPoint();
        }
    }
    m_db.commit();
}

void Indexer::pause()
{
    m_paused = true;
    m_db.setMeta(QStringLiteral("paused"), QStringLiteral("true"));
}

void Indexer::resume()
{
    m_paused = false;
    m_db.setMeta(QStringLiteral("paused"), QStringLiteral("false"));
    if (m_catchUpOwed)
        catchUp();
    flush();
}

QString Indexer::state()
{
    return m_paused ? QStringLiteral("paused") : m_db.state();
}

} // namespace Riley
