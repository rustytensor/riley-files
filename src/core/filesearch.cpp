// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "filesearch.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Riley {

namespace {

// How often a worker hands what it found to the model: as soon as it has this many rows, or
// this long after the last batch.
constexpr int BatchRows = 250;
constexpr qint64 BatchMs = 60;

// Path order, the way a person reads it: by character ignoring case, exact case breaking ties.
// Compared per character, so a folder's contents come right after its name and before a
// sibling that sorts after it (see the walk below).
int comparePaths(const QString &a, const QString &b)
{
    const int folded = a.compare(b, Qt::CaseInsensitive);
    return folded != 0 ? folded : a.compare(b, Qt::CaseSensitive);
}

bool hitBefore(const SearchHit &a, const SearchHit &b)
{
    const int byPath = comparePaths(a.relativePath, b.relativePath);
    return byPath != 0 ? byPath < 0 : a.line < b.line;
}

QString relativeTo(const QString &folder, const QString &path)
{
    if (folder == QLatin1String("/"))
        return path.mid(1);
    return path.mid(folder.size() + 1);
}

QString folderOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("/");
    return path.left(slash);
}

QString nameOf(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

// rg writes text as {"text": ...}, or as {"bytes": base64} when it is not valid UTF-8.
QByteArray rawText(const QJsonObject &object)
{
    if (object.contains(QLatin1String("text")))
        return object.value(QLatin1String("text")).toString().toUtf8();
    return QByteArray::fromBase64(object.value(QLatin1String("bytes")).toString().toLatin1());
}

// One rg "match" record as a hit: the line trimmed of its line end and leading blanks, cut to
// LineWidth characters around the first match, with the match offsets turned from bytes into
// characters of the text shown.
bool parseMatch(const QByteArray &line, const QString &folder, SearchHit &hit)
{
    const QJsonDocument document = QJsonDocument::fromJson(line);
    const QJsonObject record = document.object();
    if (record.value(QLatin1String("type")).toString() != QLatin1String("match"))
        return false;
    const QJsonObject data = record.value(QLatin1String("data")).toObject();

    hit.path = QFile::decodeName(rawText(data.value(QLatin1String("path")).toObject()));
    if (hit.path.isEmpty())
        return false;
    hit.relativePath = relativeTo(folder, hit.path);
    hit.isDir = false;
    hit.line = data.value(QLatin1String("line_number")).toInt();

    QByteArray bytes = rawText(data.value(QLatin1String("lines")).toObject());
    while (bytes.endsWith('\n') || bytes.endsWith('\r'))
        bytes.chop(1);

    QList<int> matches;
    const QJsonArray submatches = data.value(QLatin1String("submatches")).toArray();
    for (const QJsonValue &value : submatches) {
        const QJsonObject submatch = value.toObject();
        const int startByte = std::clamp(submatch.value(QLatin1String("start")).toInt(), 0, int(bytes.size()));
        const int endByte = std::clamp(submatch.value(QLatin1String("end")).toInt(), startByte, int(bytes.size()));
        const int start = int(QString::fromUtf8(bytes.left(startByte)).size());
        const int length = int(QString::fromUtf8(bytes.mid(startByte, endByte - startByte)).size());
        matches << start << length;
    }

    QString text = QString::fromUtf8(bytes);
    qsizetype lead = 0;
    while (lead < text.size() && (text.at(lead) == QLatin1Char(' ') || text.at(lead) == QLatin1Char('\t')))
        ++lead;
    qsizetype from = lead;
    if (text.size() - from > FileSearch::LineWidth && !matches.isEmpty()) {
        // Keep some of what comes before the first match, so it reads in context.
        const qsizetype firstMatch = matches.at(0);
        from = std::max(lead, std::min(firstMatch - FileSearch::LineWidth / 3,
                                       text.size() - FileSearch::LineWidth));
    }
    text = text.mid(from, FileSearch::LineWidth);

    hit.matches.clear();
    for (qsizetype i = 0; i + 1 < matches.size(); i += 2) {
        const int start = int(matches.at(i) - from);
        int length = matches.at(i + 1);
        if (start < 0 || start >= text.size())
            continue;
        length = std::min(length, int(text.size()) - start);
        hit.matches << start << length;
    }
    hit.text = text;
    return true;
}

// Whether `path` is `folder` or below it (both cleaned and absolute).
bool atOrBelow(const QString &path, const QString &folder)
{
    if (folder == QLatin1String("/"))
        return path.startsWith(QLatin1Char('/'));
    return path == folder || (path.startsWith(folder) && path.at(folder.size()) == QLatin1Char('/'));
}

// A path as a ripgrep glob that matches only itself: the glob's special characters escaped.
QString literalGlob(const QString &path)
{
    QString glob;
    for (const QChar c : path) {
        if (QStringLiteral("\\*?[]{}!").contains(c))
            glob += QLatin1Char('\\');
        glob += c;
    }
    return glob;
}

// One line of a file found through the index, as a hit: trimmed and cut the way ripgrep's
// lines are, with `spans` (start and length pairs in `line`) moved to match.
SearchHit lineHit(const QString &root, const QString &path, int number, const QString &line, const QList<int> &spans)
{
    SearchHit hit;
    hit.path = path;
    hit.relativePath = relativeTo(root, path);
    hit.line = number;
    hit.fromIndex = true;
    qsizetype lead = 0;
    while (lead < line.size() && (line.at(lead) == QLatin1Char(' ') || line.at(lead) == QLatin1Char('\t')))
        ++lead;
    qsizetype from = lead;
    if (line.size() - from > FileSearch::LineWidth && !spans.isEmpty())
        from = std::max(lead, std::min<qsizetype>(spans.at(0) - FileSearch::LineWidth / 3,
                                                  line.size() - FileSearch::LineWidth));
    hit.text = line.mid(from, FileSearch::LineWidth);
    for (qsizetype i = 0; i + 1 < spans.size(); i += 2) {
        const int start = int(spans.at(i) - from);
        if (start < 0 || start >= hit.text.size())
            continue;
        hit.matches << start << std::min(spans.at(i + 1), int(hit.text.size()) - start);
    }
    return hit;
}

// Where each of `needles` is found in `line`, ignoring case: start and length pairs, in order.
QList<int> spansIn(const QString &line, const QStringList &needles)
{
    QList<std::pair<int, int>> found;
    for (const QString &needle : needles) {
        if (needle.isEmpty())
            continue;
        for (qsizetype at = line.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0;
             at = line.indexOf(needle, at + needle.size(), Qt::CaseInsensitive))
            found.append({int(at), int(needle.size())});
    }
    std::sort(found.begin(), found.end());
    QList<int> spans;
    int end = -1;
    for (const auto &[start, length] : std::as_const(found)) {
        if (start < end)
            continue;
        spans << start << length;
        end = start + length;
    }
    return spans;
}

struct WalkEntry
{
    QByteArray name;
    QString key; // the name, with "/" after a folder's, so it sorts in path order
    bool isDir = false;
};

} // namespace

SearchIndex::~SearchIndex() = default;

FileSearch::FileSearch(QObject *parent)
    : QAbstractListModel(parent)
    , m_wanted(std::make_shared<std::atomic<quint64>>(0))
{
    m_pool.setMaxThreadCount(2);
    m_pool.setExpiryTimeout(5000);
}

FileSearch::~FileSearch()
{
    // Workers still running give up at their next check (rg is killed), and none can post to a
    // dead model.
    m_wanted->store(~quint64(0));
    m_pool.waitForDone();
}

int FileSearch::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_hits.size());
}

QVariant FileSearch::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hits.size())
        return {};
    const SearchHit &hit = m_hits.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return nameOf(hit.path);
    case PathRole:
        return hit.path;
    case RelativePathRole:
        return hit.relativePath;
    case FolderRole:
        return folderOf(hit.path);
    case PlaceRole: {
        const qsizetype slash = hit.relativePath.lastIndexOf(QLatin1Char('/'));
        return slash < 0 ? QString() : hit.relativePath.left(slash);
    }
    case IsDirRole:
        return hit.isDir;
    case LineRole:
        return hit.line;
    case TextRole:
        return hit.text;
    case MatchStartRole:
        return hit.matches.isEmpty() ? -1 : hit.matches.at(0);
    case MatchLengthRole:
        return hit.matches.size() < 2 ? 0 : hit.matches.at(1);
    case MatchesRole: {
        QVariantList list;
        for (int value : hit.matches)
            list.append(value);
        return list;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> FileSearch::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {RelativePathRole, "relativePath"},
        {FolderRole, "folder"},
        {PlaceRole, "place"},
        {IsDirRole, "isDir"},
        {LineRole, "line"},
        {TextRole, "text"},
        {MatchStartRole, "matchStart"},
        {MatchLengthRole, "matchLength"},
        {MatchesRole, "matches"},
    };
}

QString FileSearch::note() const
{
    if (!m_capped)
        return {};
    return tr("Showing the first %1 results.").arg(QLocale().toString(Limit));
}

QString FileSearch::missingRipgrepMessage()
{
    return tr("Searching inside files needs ripgrep (rg).");
}

QString FileSearch::pathAt(int row) const
{
    return row >= 0 && row < m_hits.size() ? m_hits.at(row).path : QString();
}

QString FileSearch::folderAt(int row) const
{
    return row >= 0 && row < m_hits.size() ? folderOf(m_hits.at(row).path) : QString();
}

QVariantMap FileSearch::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_hits.size())
        return map;
    const QModelIndex at = index(row);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(at, it.key()));
    return map;
}

QSet<Qt::HANDLE> FileSearch::searchThreads() const
{
    QMutexLocker locker(&m_threadsLock);
    return m_threads;
}

void FileSearch::setIndex(std::shared_ptr<SearchIndex> index)
{
    QMutexLocker locker(&m_indexLock);
    m_index = std::move(index);
}

std::shared_ptr<SearchIndex> FileSearch::searchIndex() const
{
    QMutexLocker locker(&m_indexLock);
    return m_index;
}

void FileSearch::noteThread()
{
    QMutexLocker locker(&m_threadsLock);
    m_threads.insert(QThread::currentThreadId());
}

void FileSearch::cancel()
{
    ++m_generation;
    m_wanted->store(m_generation);
}

void FileSearch::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void FileSearch::start(const QString &folder, const QString &query, Mode mode, bool showHidden)
{
    cancel();
    const bool hadRows = !m_hits.isEmpty();
    const bool hadNote = m_capped;
    const bool hadMessage = !m_message.isEmpty();
    beginResetModel();
    m_hits.clear();
    endResetModel();
    m_capped = false;
    m_message.clear();
    m_folder = QDir::cleanPath(folder);
    m_query = query;
    m_mode = mode;
    emit started();
    if (hadRows)
        emit countChanged();
    if (hadNote)
        emit noteChanged();
    if (hadMessage)
        emit messageChanged();

    if (query.isEmpty() || m_folder.isEmpty()) {
        setRunning(false);
        return;
    }
    setRunning(true);

    const quint64 generation = m_generation;
    const QString root = m_folder;
    if (mode == Names)
        m_pool.start([this, generation, root, query, showHidden] { runNames(generation, root, query, showHidden); });
    else
        m_pool.start([this, generation, root, query, showHidden] { runContents(generation, root, query, showHidden); });
}

void FileSearch::stop()
{
    cancel();
    setRunning(false);
}

void FileSearch::clear()
{
    cancel();
    const bool hadRows = !m_hits.isEmpty();
    const bool hadNote = m_capped;
    const bool hadMessage = !m_message.isEmpty();
    beginResetModel();
    m_hits.clear();
    endResetModel();
    m_capped = false;
    m_message.clear();
    m_query.clear();
    setRunning(false);
    if (hadRows)
        emit countChanged();
    if (hadNote)
        emit noteChanged();
    if (hadMessage)
        emit messageChanged();
}

// Runs on a worker thread. The model may only be touched on its own thread, so everything goes
// back through a queued call, and is dropped there if a newer search began meanwhile.
void FileSearch::post(quint64 generation, QList<SearchHit> hits, bool done, bool capped, QString message)
{
    if (m_wanted->load() != generation)
        return;
    QMetaObject::invokeMethod(
        this,
        [this, generation, hits = std::move(hits), done, capped, message = std::move(message)] {
            receive(generation, hits, done, capped, message);
        },
        Qt::QueuedConnection);
}

void FileSearch::receive(quint64 generation, const QList<SearchHit> &batch, bool done, bool capped,
                         const QString &message)
{
    if (generation != m_generation)
        return; // from a search that was stopped or replaced: never shown

    QList<SearchHit> hits = batch;
    std::stable_sort(hits.begin(), hits.end(), hitBefore);
    const qsizetype room = Limit - m_hits.size();
    if (hits.size() > room) {
        // Keep the ones that come first in path order.
        hits.resize(std::max<qsizetype>(room, 0));
        capped = true;
    }

    if (!hits.isEmpty()) {
        if (m_hits.isEmpty() || !hitBefore(hits.first(), m_hits.last())) {
            const int first = int(m_hits.size());
            beginInsertRows(QModelIndex(), first, first + int(hits.size()) - 1);
            m_hits.append(hits);
            endInsertRows();
        } else {
            for (const SearchHit &hit : std::as_const(hits)) {
                const auto at = std::upper_bound(m_hits.cbegin(), m_hits.cend(), hit, hitBefore);
                const int row = int(at - m_hits.cbegin());
                beginInsertRows(QModelIndex(), row, row);
                m_hits.insert(row, hit);
                endInsertRows();
            }
        }
        emit countChanged();
        emit batchAdded(generation, int(hits.size()));
    }

    if (capped && !m_capped) {
        m_capped = true;
        emit noteChanged();
    }
    if (!message.isEmpty() && message != m_message) {
        m_message = message;
        emit messageChanged();
    }
    if (capped && !done) {
        // The rows are full: the worker has nothing more to give.
        cancel();
        done = true;
    }
    if (done) {
        setRunning(false);
        emit finished(generation);
    }
}

bool FileSearch::namesFromIndex(quint64 generation, const QString &root, const QString &query, bool showHidden)
{
    const std::shared_ptr<SearchIndex> source = searchIndex();
    QList<SearchIndex::Name> names;
    if (!source || !source->names(root, query, showHidden, Limit + 1, names))
        return false;
    QList<SearchHit> hits;
    hits.reserve(names.size());
    for (const SearchIndex::Name &name : std::as_const(names)) {
        if (name.path == root || !atOrBelow(name.path, root))
            continue;
        SearchHit hit;
        hit.path = name.path;
        hit.relativePath = relativeTo(root, name.path);
        hit.isDir = name.isDir;
        hit.fromIndex = true;
        hits.append(std::move(hit));
    }
    std::sort(hits.begin(), hits.end(), hitBefore);
    const bool capped = hits.size() > Limit;
    if (capped)
        hits.resize(Limit);
    post(generation, std::move(hits), true, capped, {});
    return true;
}

void FileSearch::runNames(quint64 generation, QString root, QString query, bool showHidden)
{
    noteThread();
    const auto stale = [this, generation] { return m_wanted->load() != generation; };
    // A search typed over before its turn came is not run at all.
    if (stale() || namesFromIndex(generation, root, query, showHidden))
        return;

    QList<SearchHit> pending;
    QElapsedTimer sincePost;
    sincePost.start();
    int found = 0;
    bool capped = false;

    const auto flush = [&](bool force) {
        if (pending.isEmpty() || (!force && pending.size() < BatchRows && sincePost.elapsed() < BatchMs))
            return;
        post(generation, std::exchange(pending, {}), false, false, {});
        sincePost.restart();
    };

    struct stat rootStat;
    const QByteArray rootBytes = QFile::encodeName(root);
    if (::stat(rootBytes.constData(), &rootStat) != 0 || !S_ISDIR(rootStat.st_mode)) {
        post(generation, {}, true, false, {});
        return;
    }
    const dev_t device = rootStat.st_dev;

    // Depth first, each folder's entries sorted by name with a "/" after folder names, gives
    // the whole tree in path order: a folder's contents come right after the folder.
    const auto visit = [&](auto &self, const QByteArray &dirBytes, const QString &relative) -> void {
        if (capped || stale())
            return;
        DIR *dir = ::opendir(dirBytes.constData());
        if (!dir)
            return;
        const int fd = ::dirfd(dir);
        std::vector<WalkEntry> entries;
        int seen = 0;
        while (const dirent *item = ::readdir(dir)) {
            if ((++seen & 255) == 0 && stale())
                break;
            const char *name = item->d_name;
            if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
                continue;
            if (name[0] == '.' && !showHidden)
                continue;
            bool isDir = false;
            if (item->d_type == DT_DIR) {
                isDir = true;
            } else if (item->d_type == DT_UNKNOWN) {
                struct stat info;
                if (::fstatat(fd, name, &info, AT_SYMLINK_NOFOLLOW) == 0)
                    isDir = S_ISDIR(info.st_mode);
            }
            WalkEntry entry;
            entry.name = QByteArray(name);
            entry.key = QFile::decodeName(entry.name);
            entry.isDir = isDir;
            if (isDir)
                entry.key.append(QLatin1Char('/'));
            entries.push_back(std::move(entry));
        }
        // Whether a folder is on the same filesystem, asked before the folder is closed.
        std::vector<bool> sameDevice(entries.size(), false);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].isDir)
                continue;
            struct stat info;
            sameDevice[i] = ::fstatat(fd, entries[i].name.constData(), &info, AT_SYMLINK_NOFOLLOW) == 0
                            && S_ISDIR(info.st_mode) && info.st_dev == device;
        }
        ::closedir(dir);
        if (stale())
            return;

        std::vector<std::size_t> order(entries.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&entries](std::size_t a, std::size_t b) {
            return comparePaths(entries[a].key, entries[b].key) < 0;
        });

        for (std::size_t i : order) {
            if (capped || stale())
                return;
            const WalkEntry &entry = entries[i];
            QString name = entry.key;
            if (entry.isDir)
                name.chop(1);
            const QString childRelative = relative.isEmpty() ? name : relative + QLatin1Char('/') + name;
            if (name.contains(query, Qt::CaseInsensitive)) {
                SearchHit hit;
                hit.relativePath = childRelative;
                hit.path = root == QLatin1String("/") ? QLatin1Char('/') + childRelative
                                                      : root + QLatin1Char('/') + childRelative;
                hit.isDir = entry.isDir;
                if (found == Limit) {
                    capped = true; // one more than is kept: the rows are full
                    return;
                }
                ++found;
                pending.append(std::move(hit));
                flush(false);
            }
            if (entry.isDir && sameDevice[i])
                self(self, dirBytes + '/' + entry.name, childRelative);
        }
        flush(false);
    };
    visit(visit, rootBytes, QString());

    if (stale())
        return;
    post(generation, std::exchange(pending, {}), true, capped, {});
}

QList<SearchHit> FileSearch::indexedLines(const QString &root, const QString &query, bool showHidden,
                                          const std::function<bool()> &stale, bool &capped)
{
    QList<SearchHit> hits;
    const std::shared_ptr<SearchIndex> source = searchIndex();
    QStringList files;
    if (!source || !source->contents(root, query, showHidden, Limit + 1, files))
        return hits;
    const QStringList words = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // The files the index names are read again, so the lines shown are the ones on disk now.
    constexpr qint64 MaxRead = 64 * 1024 * 1024;
    constexpr int PerFile = 5; // as many as ripgrep is asked for
    for (const QString &path : std::as_const(files)) {
        if (stale())
            return {};
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > MaxRead)
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        const QStringList lines = text.split(QLatin1Char('\n'));
        // The whole query where a line holds it, as ripgrep would find it; else the lines that
        // hold its words.
        for (const QStringList &needles : {QStringList{query}, words}) {
            int taken = 0;
            for (qsizetype i = 0; i < lines.size() && taken < PerFile; ++i) {
                QString line = lines.at(i);
                if (line.endsWith(QLatin1Char('\r')))
                    line.chop(1);
                const QList<int> spans = spansIn(line, needles);
                if (spans.isEmpty())
                    continue;
                if (hits.size() == Limit) {
                    capped = true;
                    return hits;
                }
                hits.append(lineHit(root, path, int(i + 1), line, spans));
                ++taken;
            }
            if (taken > 0)
                break;
        }
    }
    return hits;
}

void FileSearch::runContents(quint64 generation, QString root, QString query, bool showHidden)
{
    noteThread();
    const auto stale = [this, generation] { return m_wanted->load() != generation; };
    if (stale())
        return;

    // The folders whose contents the index holds are answered from it; ripgrep looks through
    // the rest.
    const std::shared_ptr<SearchIndex> source = searchIndex();
    const QStringList covered = source ? source->contentFolders(root) : QStringList();
    int found = 0;
    bool capped = false;
    if (!covered.isEmpty()) {
        QList<SearchHit> hits = indexedLines(root, query, showHidden, stale, capped);
        if (stale())
            return;
        found = int(hits.size());
        const bool whole = std::any_of(covered.begin(), covered.end(),
                                       [&root](const QString &folder) { return atOrBelow(root, folder); });
        if (whole || capped) {
            post(generation, std::move(hits), true, capped, {});
            return;
        }
        if (!hits.isEmpty())
            post(generation, std::move(hits), false, false, {});
    }

    const QString program = QStandardPaths::findExecutable(QStringLiteral("rg"));
    if (program.isEmpty()) {
        post(generation, {}, true, false, missingRipgrepMessage());
        return;
    }

    QStringList arguments{
        QStringLiteral("--json"),
        QStringLiteral("--no-config"),
        QStringLiteral("--ignore-case"),
        QStringLiteral("--fixed-strings"),
        QStringLiteral("--max-columns"), QString::number(LineWidth),
        QStringLiteral("--max-count"), QStringLiteral("5"),
        QStringLiteral("--one-file-system"),
    };
    if (showHidden)
        arguments << QStringLiteral("--hidden");
    for (const QString &folder : std::as_const(covered))
        arguments << QStringLiteral("--glob") << QLatin1Char('!') + literalGlob(folder);
    arguments << QStringLiteral("--regexp") << query << QStringLiteral("--") << root;
    const auto inCovered = [&covered](const QString &path) {
        return std::any_of(covered.begin(), covered.end(),
                           [&path](const QString &folder) { return atOrBelow(path, folder); });
    };

    QProcess rg;
    rg.setProgram(program);
    rg.setArguments(arguments);
    rg.setStandardInputFile(QProcess::nullDevice());
    rg.setStandardErrorFile(QProcess::nullDevice());
    rg.start(QIODevice::ReadOnly);
    if (!rg.waitForStarted(5000)) {
        post(generation, {}, true, false, tr("ripgrep (rg) could not be started."));
        return;
    }

    QList<SearchHit> pending;
    QElapsedTimer sincePost;
    sincePost.start();
    QByteArray buffer;

    const auto takeLines = [&] {
        qsizetype from = 0;
        for (;;) {
            const qsizetype end = buffer.indexOf('\n', from);
            if (end < 0)
                break;
            SearchHit hit;
            if (parseMatch(buffer.mid(from, end - from), root, hit) && !inCovered(hit.path)) {
                if (found == Limit) {
                    capped = true; // one more than is kept: the rows are full
                    break;
                }
                ++found;
                pending.append(std::move(hit));
            }
            from = end + 1;
        }
        buffer.remove(0, from);
    };

    for (;;) {
        if (stale()) {
            rg.kill();
            rg.waitForFinished(2000);
            return;
        }
        const bool more = rg.waitForReadyRead(50);
        buffer += rg.readAllStandardOutput();
        takeLines();
        if (capped)
            break;
        if (!more && rg.state() == QProcess::NotRunning) {
            buffer += rg.readAllStandardOutput();
            if (!buffer.isEmpty() && !buffer.endsWith('\n'))
                buffer.append('\n');
            takeLines();
            break;
        }
        if (!pending.isEmpty() && (pending.size() >= BatchRows || sincePost.elapsed() >= BatchMs)) {
            post(generation, std::exchange(pending, {}), false, false, {});
            sincePost.restart();
        }
    }
    if (rg.state() != QProcess::NotRunning) {
        rg.kill();
        rg.waitForFinished(2000);
    }
    if (stale())
        return;
    post(generation, std::exchange(pending, {}), true, capped, {});
}

} // namespace Riley
