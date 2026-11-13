// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "foldermodel.h"

#include "folderlisting.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>

#include <algorithm>
#include <iterator>

namespace Riley {

namespace {

// How long changes on disk are gathered before a folder is read again.
constexpr int DebounceMs = 100;

const QChar BranchMid(0x251C);  // ├
const QChar BranchLast(0x2514); // └
const QChar Through(0x2502);    // │

QString cleanPath(const QString &path)
{
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString categoryOf(const QMimeType &mime)
{
    const QString name = mime.name();
    if (name.startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (name.startsWith(QLatin1String("audio/")))
        return QStringLiteral("audio");
    if (name.startsWith(QLatin1String("video/")))
        return QStringLiteral("video");

    static const QStringList documents = {
        QStringLiteral("application/pdf"),
        QStringLiteral("application/epub+zip"),
        QStringLiteral("application/msword"),
        QStringLiteral("application/rtf"),
        QStringLiteral("application/vnd.ms-excel"),
        QStringLiteral("application/vnd.ms-powerpoint"),
    };
    if (documents.contains(name) || name.startsWith(QLatin1String("application/vnd.oasis.opendocument"))
        || name.startsWith(QLatin1String("application/vnd.openxmlformats")))
        return QStringLiteral("document");

    static const QStringList archives = {
        QStringLiteral("application/zip"),
        QStringLiteral("application/x-tar"),
        QStringLiteral("application/gzip"),
        QStringLiteral("application/x-7z-compressed"),
        QStringLiteral("application/x-xz"),
        QStringLiteral("application/x-bzip"),
        QStringLiteral("application/x-bzip2"),
        QStringLiteral("application/zstd"),
        QStringLiteral("application/vnd.rar"),
        QStringLiteral("application/x-rar"),
        QStringLiteral("application/x-compressed-tar"),
        QStringLiteral("application/x-cd-image"),
        QStringLiteral("application/vnd.efi.iso"),
        QStringLiteral("application/x-rpm"),
    };
    for (const QString &archive : archives) {
        if (name == archive || mime.inherits(archive))
            return QStringLiteral("archive");
    }

    static const QStringList code = {
        QStringLiteral("application/x-shellscript"),
        QStringLiteral("application/javascript"),
        QStringLiteral("application/json"),
        QStringLiteral("application/xml"),
        QStringLiteral("application/x-executable"),
        QStringLiteral("application/x-sharedlib"),
        QStringLiteral("text/css"),
        QStringLiteral("text/html"),
    };
    if (code.contains(name) || name.startsWith(QLatin1String("text/x-")))
        return QStringLiteral("code");

    if (name.startsWith(QLatin1String("text/")) || mime.inherits(QStringLiteral("text/plain")))
        return QStringLiteral("text");
    return QStringLiteral("file");
}

} // namespace

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_wanted(std::make_shared<std::atomic<quint64>>(0))
{
    m_root.open = true;
    m_root.entry.isDir = true;

    // Two readers: enough for the folder and a tree folder opened next to it.
    m_pool.setMaxThreadCount(2);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(DebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &FolderModel::refreshDirtyFolders);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &FolderModel::folderChangedOnDisk);
}

FolderModel::~FolderModel()
{
    // Readers still running give up at their next check, and none can post to a dead model.
    m_wanted->store(~quint64(0));
    m_pool.waitForDone();
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeAt(index.row());
    if (!node || index.parent().isValid())
        return {};
    const DirEntry &e = node->entry;

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case PathRole:
        return e.path;
    case IsDirRole:
        return e.isDir;
    case HiddenRole:
        return e.hidden;
    case SizeRole:
        return e.size;
    case SizeTextRole:
        if (e.isDir)
            return QString();
        return QLocale().formattedDataSize(e.size, 1, QLocale::DataSizeTraditionalFormat);
    case ModifiedRole:
        return e.modified;
    case ModifiedTextRole: {
        if (!e.modified.isValid())
            return QString();
        const QLocale locale;
        const QDate day = e.modified.date();
        const QDate today = QDate::currentDate();
        const QString time = locale.toString(e.modified.time(), QLocale::ShortFormat);
        if (day == today)
            return tr("Today %1").arg(time);
        if (day == today.addDays(-1))
            return tr("Yesterday %1").arg(time);
        return locale.toString(day, QLocale::ShortFormat) + QLatin1Char(' ') + time;
    }
    case ExtensionRole:
        return e.isDir ? QString() : EntryOrder::extensionOf(e.name).toString();
    case KindTextRole:
        return typeInfo(e).kindText;
    case CategoryRole:
        return typeInfo(e).category;
    case IconNameRole:
        return typeInfo(e).iconName;
    case DepthRole:
        return node->depth;
    case OpenRole:
        return node->open;
    case LinesRole:
        return linesFor(node);
    case SelectedRole:
        return m_selected.contains(e.path);
    default:
        return {};
    }
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IsDirRole, "isDir"},
        {HiddenRole, "hidden"},
        {SizeRole, "size"},
        {SizeTextRole, "sizeText"},
        {ModifiedRole, "modified"},
        {ModifiedTextRole, "modifiedText"},
        {ExtensionRole, "extension"},
        {KindTextRole, "kindText"},
        {CategoryRole, "category"},
        {IconNameRole, "iconName"},
        {DepthRole, "depth"},
        {OpenRole, "open"},
        {LinesRole, "lines"},
        {SelectedRole, "selected"},
    };
}

void FolderModel::setPath(const QString &path)
{
    const QString clean = cleanPath(path);
    if (clean == m_root.entry.path)
        return;

    // Work for the old folder is dropped, wherever it is.
    ++m_generation;
    m_wanted->store(m_generation);

    beginResetModel();
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    m_rows.clear();
    m_root.children.clear();
    m_byPath.clear();
    m_openPaths.clear();
    m_pending.clear();
    m_dirty.clear();
    if (!m_selected.isEmpty()) {
        m_selected.clear();
        m_selectionDirty = true;
    }
    m_root.entry = DirEntry();
    m_root.entry.path = clean;
    m_root.entry.name = QFileInfo(clean).fileName();
    m_root.entry.isDir = true;
    m_root.loaded = false;
    endResetModel();

    setError({});
    emit pathChanged();

    if (!clean.isEmpty()) {
        watch(clean);
        requestListing(&m_root);
    }
    settle();
}

void FolderModel::setSortKey(Sort key)
{
    const SortKey wanted = SortKey(int(key));
    if (wanted == m_sortKey)
        return;
    m_sortKey = wanted;
    relayout();
    refreshLines();
    emit sortKeyChanged();
}

void FolderModel::setShowHidden(bool show)
{
    if (show == m_showHidden)
        return;
    m_showHidden = show;
    emit showHiddenChanged();
    reload();
}

bool FolderModel::hasOpenFolders() const
{
    return std::any_of(m_root.children.begin(), m_root.children.end(),
                       [](const std::unique_ptr<Node> &child) { return child->open; });
}

void FolderModel::reload()
{
    if (m_root.entry.path.isEmpty())
        return;
    requestListing(&m_root);
    for (Node *node : std::as_const(m_rows)) {
        if (node->open)
            requestListing(node);
    }
    settle();
}

QString FolderModel::pathAt(int row) const
{
    const Node *node = nodeAt(row);
    return node ? node->entry.path : QString();
}

QString FolderModel::nameAt(int row) const
{
    const Node *node = nodeAt(row);
    return node ? node->entry.name : QString();
}

bool FolderModel::isDirAt(int row) const
{
    const Node *node = nodeAt(row);
    return node && node->entry.isDir;
}

bool FolderModel::isOpenAt(int row) const
{
    const Node *node = nodeAt(row);
    return node && node->open;
}

int FolderModel::depthAt(int row) const
{
    const Node *node = nodeAt(row);
    return node ? node->depth : -1;
}

QVariantMap FolderModel::get(int row) const
{
    QVariantMap map;
    if (!nodeAt(row))
        return map;
    const QModelIndex idx = index(row);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
    return map;
}

int FolderModel::rowOfPath(const QString &path) const
{
    const Node *node = m_byPath.value(cleanPath(path));
    return node ? rowOf(node) : -1;
}

int FolderModel::findByPrefix(const QString &prefix, int from) const
{
    const int n = count();
    if (prefix.isEmpty() || n == 0)
        return -1;
    from = std::clamp(from, 0, n - 1);
    for (int k = 0; k < n; ++k) {
        const int row = (from + k) % n;
        if (m_rows.at(row)->entry.name.startsWith(prefix, Qt::CaseInsensitive))
            return row;
    }
    return -1;
}

bool FolderModel::expand(int row)
{
    Node *node = nodeAt(row);
    if (!node || !node->entry.isDir || node->open)
        return false;
    m_openPaths.insert(node->entry.path);
    openNode(node, row);
    settle();
    return true;
}

bool FolderModel::collapse(int row)
{
    Node *node = nodeAt(row);
    if (!node || !node->open)
        return false;
    m_openPaths.remove(node->entry.path);
    closeNode(node, row);
    refreshLines();
    settle();
    return true;
}

bool FolderModel::toggleOpen(int row)
{
    return isOpenAt(row) ? collapse(row) : expand(row);
}

void FolderModel::collapseAll()
{
    m_openPaths.clear();
    auto &kids = m_root.children;
    std::vector<int> starts(kids.size());
    int row = 0;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        starts[i] = row;
        row += 1 + shownBelow(kids[i].get());
    }
    for (std::size_t i = kids.size(); i-- > 0;) {
        if (kids[i]->open)
            closeNode(kids[i].get(), starts[i]);
    }
    settle();
}

int FolderModel::parentRow(int row) const
{
    const Node *node = nodeAt(row);
    if (!node || node->parent == &m_root)
        return -1;
    return rowOf(node->parent);
}

bool FolderModel::isLoadedAt(int row) const
{
    const Node *node = nodeAt(row);
    return node && node->open && node->loaded;
}

QStringList FolderModel::selectedPaths() const
{
    // In row order, so actions on the selection go top to bottom.
    QStringList paths;
    if (m_selected.isEmpty())
        return paths;
    for (const Node *node : m_rows) {
        if (m_selected.contains(node->entry.path))
            paths.append(node->entry.path);
    }
    return paths;
}

bool FolderModel::isSelected(int row) const
{
    const Node *node = nodeAt(row);
    return node && m_selected.contains(node->entry.path);
}

void FolderModel::selectOnly(int row)
{
    const Node *node = nodeAt(row);
    m_selected.clear();
    if (node)
        m_selected.insert(node->entry.path);
    m_selectionDirty = true;
    settle();
}

void FolderModel::setSelected(int row, bool selected)
{
    const Node *node = nodeAt(row);
    if (!node || m_selected.contains(node->entry.path) == selected)
        return;
    if (selected)
        m_selected.insert(node->entry.path);
    else
        m_selected.remove(node->entry.path);
    m_selectionDirty = true;
    settle();
}

void FolderModel::toggleSelected(int row)
{
    setSelected(row, !isSelected(row));
}

void FolderModel::selectRange(int from, int to)
{
    if (m_rows.isEmpty())
        return;
    const int last = count() - 1;
    int a = std::clamp(std::min(from, to), 0, last);
    const int b = std::clamp(std::max(from, to), 0, last);
    for (; a <= b; ++a)
        m_selected.insert(m_rows.at(a)->entry.path);
    m_selectionDirty = true;
    settle();
}

void FolderModel::selectAll()
{
    selectRange(0, count() - 1);
}

void FolderModel::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    m_selectionDirty = true;
    settle();
}

// ---- rows and nodes ------------------------------------------------------------------------

FolderModel::Node *FolderModel::nodeAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return nullptr;
    return m_rows.at(row);
}

int FolderModel::rowOf(const Node *node) const
{
    if (node == &m_root)
        return -1;
    return int(m_rows.indexOf(const_cast<Node *>(node)));
}

int FolderModel::shownBelow(const Node *node) const
{
    if (!node->open)
        return 0;
    int n = 0;
    for (const auto &child : node->children)
        n += 1 + shownBelow(child.get());
    return n;
}

FolderModel::Node *FolderModel::folderNode(const QString &path)
{
    if (path == m_root.entry.path)
        return &m_root;
    Node *node = m_byPath.value(path);
    return node && node->open ? node : nullptr;
}

QString FolderModel::linesFor(const Node *node) const
{
    if (node->depth <= 0)
        return {};
    auto isLast = [](const Node *n) { return n->parent->children.back().get() == n; };

    QString lines(node->depth, QLatin1Char(' '));
    lines[node->depth - 1] = isLast(node) ? BranchLast : BranchMid;
    const Node *ancestor = node->parent;
    for (int level = node->depth - 2; level >= 0; --level) {
        lines[level] = isLast(ancestor) ? QChar(QLatin1Char(' ')) : Through;
        ancestor = ancestor->parent;
    }
    return lines;
}

// ---- reading folders -----------------------------------------------------------------------

QSet<Qt::HANDLE> FolderModel::listingThreads() const
{
    QMutexLocker locker(&m_threadsLock);
    return m_listingThreads;
}

void FolderModel::requestListing(Node *folder)
{
    const QString path = folder->entry.path;
    const quint64 request = ++m_nextRequest;
    m_pending.insert(path, request);

    const quint64 generation = m_generation;
    const std::shared_ptr<std::atomic<quint64>> wanted = m_wanted;
    ListingOptions options;
    options.showHidden = m_showHidden;
    const SortKey key = m_sortKey;

    m_pool.start([this, wanted, generation, request, path, options, key] {
        {
            QMutexLocker locker(&m_threadsLock);
            m_listingThreads.insert(QThread::currentThreadId());
        }
        const auto stale = [&wanted, generation] { return wanted->load() != generation; };
        ListingResult result = listFolder(path, options, key, stale);
        if (stale())
            return;
        // The model waits for this thread in its destructor, so it is alive here.
        QMetaObject::invokeMethod(
            this,
            [this, generation, request, path, key, result = std::move(result)] {
                applyListing(generation, request, path, key, result);
            },
            Qt::QueuedConnection);
    });
}

void FolderModel::applyListing(quint64 generation, quint64 request, const QString &folderPath,
                               SortKey key, ListingResult result)
{
    if (generation != m_generation)
        return;
    const auto pending = m_pending.constFind(folderPath);
    if (pending == m_pending.cend() || pending.value() != request)
        return; // a newer reading of this folder is on its way
    m_pending.erase(pending);

    Node *folder = folderNode(folderPath);
    if (folder) {
        if (key != m_sortKey)
            EntryOrder(m_sortKey).sort(result.entries);
        if (folder == &m_root)
            setError(result.error);
        mergeChildren(folder, result.entries);
        folder->loaded = true;
        openRememberedChildren(folder);
    }
    settle();

    if (folder) {
        emit folderLoaded(folderPath);
        if (folder == &m_root)
            emit loaded();
    }
}

// Brings a folder's rows in line with a fresh reading, as removals, changes and inserts, so the
// rows that stay keep their place (and open folders stay open).
void FolderModel::mergeChildren(Node *folder, const QList<DirEntry> &entries)
{
    auto &kids = folder->children;
    const int first = rowOf(folder) + 1;

    if (kids.empty()) {
        if (!entries.isEmpty()) {
            insertChildren(folder, 0, first, entries);
            refreshLines();
        }
        return;
    }

    QHash<QString, qsizetype> fresh;
    fresh.reserve(entries.size());
    for (qsizetype j = 0; j < entries.size(); ++j)
        fresh.insert(entries.at(j).name, j);

    // Gone (or turned from file to folder or back): removed from the bottom up, so the rows
    // worked out first stay right.
    bool changed = false;
    std::vector<int> starts(kids.size());
    int row = first;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        starts[i] = row;
        row += 1 + shownBelow(kids[i].get());
    }
    for (std::size_t i = kids.size(); i-- > 0;) {
        const auto found = fresh.constFind(kids[i]->entry.name);
        if (found == fresh.cend() || entries.at(found.value()).isDir != kids[i]->entry.isDir) {
            removeChild(folder, i, starts[i]);
            changed = true;
        }
    }

    // Still there: take the new size and time, and see whether the order still holds.
    const EntryOrder order(m_sortKey);
    bool reorder = false;
    row = first;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        Node *node = kids[i].get();
        const DirEntry &now = entries.at(fresh.value(node->entry.name));
        if (node->entry != now) {
            node->entry = now;
            emit dataChanged(index(row), index(row));
        }
        if (i > 0 && !order.lessThan(kids[i - 1]->entry, node->entry))
            reorder = true;
        row += 1 + shownBelow(node);
    }
    if (reorder)
        relayout();

    // New ones: the rows kept are now in the same order as the reading, so the new names
    // fall into the gaps between them.
    row = first;
    std::size_t i = 0;
    qsizetype j = 0;
    while (j < entries.size()) {
        if (i < kids.size() && kids[i]->entry.name == entries.at(j).name) {
            row += 1 + shownBelow(kids[i].get());
            ++i;
            ++j;
            continue;
        }
        qsizetype end = j;
        while (end < entries.size() && !(i < kids.size() && kids[i]->entry.name == entries.at(end).name))
            ++end;
        insertChildren(folder, i, row, entries.mid(j, end - j));
        i += std::size_t(end - j);
        row += int(end - j);
        j = end;
        changed = true;
    }

    if (changed)
        refreshLines();
}

void FolderModel::insertChildren(Node *folder, std::size_t at, int row, const QList<DirEntry> &entries)
{
    if (entries.isEmpty())
        return;
    beginInsertRows(QModelIndex(), row, row + int(entries.size()) - 1);

    std::vector<std::unique_ptr<Node>> made;
    made.reserve(std::size_t(entries.size()));
    QList<Node *> rows;
    rows.reserve(entries.size());
    for (const DirEntry &entry : entries) {
        auto node = std::make_unique<Node>();
        node->entry = entry;
        node->parent = folder;
        node->depth = folder->depth + 1;
        m_byPath.insert(entry.path, node.get());
        rows.append(node.get());
        made.push_back(std::move(node));
    }
    auto &kids = folder->children;
    kids.insert(kids.begin() + std::ptrdiff_t(at), std::make_move_iterator(made.begin()),
                std::make_move_iterator(made.end()));

    if (m_rows.isEmpty())
        m_rows = std::move(rows);
    else
        m_rows = m_rows.first(row) + rows + m_rows.sliced(row);

    endInsertRows();
}

void FolderModel::removeChild(Node *folder, std::size_t at, int row)
{
    auto &kids = folder->children;
    Node *node = kids[at].get();
    const int n = 1 + shownBelow(node);
    beginRemoveRows(QModelIndex(), row, row + n - 1);
    m_rows.remove(row, n);
    forgetSubtree(node, true);
    kids.erase(kids.begin() + std::ptrdiff_t(at));
    endRemoveRows();
}

// Drops what the model keeps about a node and everything below it. `gone` means it left the
// disk, so open folders and selections under it are forgotten too.
void FolderModel::forgetSubtree(Node *node, bool gone)
{
    for (const auto &child : node->children)
        forgetSubtree(child.get(), gone);

    const QString &path = node->entry.path;
    if (m_byPath.value(path) == node)
        m_byPath.remove(path);
    if (node->open) {
        unwatch(path);
        m_pending.remove(path);
        m_dirty.remove(path);
    }
    if (gone) {
        const QString inside = path + QLatin1Char('/');
        m_openPaths.remove(path);
        m_openPaths.removeIf([&inside](const QString &p) { return p.startsWith(inside); });
        if (m_selected.remove(path))
            m_selectionDirty = true;
    }
}

void FolderModel::openNode(Node *node, int row)
{
    node->open = true;
    node->loaded = false;
    watch(node->entry.path);
    requestListing(node);
    emit dataChanged(index(row), index(row), {OpenRole});
}

void FolderModel::closeNode(Node *node, int row)
{
    const int n = shownBelow(node);
    if (n > 0) {
        beginRemoveRows(QModelIndex(), row + 1, row + n);
        m_rows.remove(row + 1, n);
    }
    for (const auto &child : node->children)
        forgetSubtree(child.get(), false);
    node->children.clear();
    if (n > 0)
        endRemoveRows();

    node->open = false;
    node->loaded = false;
    unwatch(node->entry.path);
    m_pending.remove(node->entry.path);
    m_dirty.remove(node->entry.path);
    emit dataChanged(index(row), index(row), {OpenRole});
}

void FolderModel::openRememberedChildren(Node *folder)
{
    for (const auto &child : folder->children) {
        if (child->entry.isDir && !child->open && m_openPaths.contains(child->entry.path))
            openNode(child.get(), rowOf(child.get()));
    }
}

void FolderModel::sortChildren(Node *folder, const EntryOrder &order)
{
    std::stable_sort(folder->children.begin(), folder->children.end(),
                     [&order](const std::unique_ptr<Node> &a, const std::unique_ptr<Node> &b) {
                         return order.lessThan(a->entry, b->entry);
                     });
    for (const auto &child : folder->children) {
        if (child->open)
            sortChildren(child.get(), order);
    }
}

void FolderModel::appendRows(const Node *folder, QList<Node *> &rows) const
{
    for (const auto &child : folder->children) {
        rows.append(child.get());
        if (child->open)
            appendRows(child.get(), rows);
    }
}

// Puts every row in the current sort order, keeping views' places on the same items.
void FolderModel::relayout()
{
    emit layoutAboutToBeChanged();
    const QModelIndexList before = persistentIndexList();
    QList<Node *> held;
    held.reserve(before.size());
    for (const QModelIndex &idx : before)
        held.append(nodeAt(idx.row()));

    sortChildren(&m_root, EntryOrder(m_sortKey));
    QList<Node *> rows;
    rows.reserve(m_rows.size());
    appendRows(&m_root, rows);
    m_rows = std::move(rows);

    if (!before.isEmpty()) {
        QHash<const Node *, int> rowFor;
        rowFor.reserve(m_rows.size());
        for (int r = 0; r < m_rows.size(); ++r)
            rowFor.insert(m_rows.at(r), r);
        QModelIndexList after;
        after.reserve(before.size());
        for (const Node *node : std::as_const(held))
            after.append(node ? index(rowFor.value(node)) : QModelIndex());
        changePersistentIndexList(before, after);
    }
    emit layoutChanged();
}

void FolderModel::refreshLines()
{
    if (!m_rows.isEmpty() && hasOpenFolders())
        emit dataChanged(index(0), index(count() - 1), {LinesRole});
}

void FolderModel::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

// ---- watching ------------------------------------------------------------------------------

void FolderModel::watch(const QString &path)
{
    // A folder that cannot be read cannot be watched either; trying only prints a warning.
    const QFileInfo info(path);
    if (info.isDir() && info.isReadable())
        m_watcher.addPaths({path});
}

void FolderModel::unwatch(const QString &path)
{
    m_watcher.removePaths({path});
}

void FolderModel::folderChangedOnDisk(const QString &path)
{
    m_dirty.insert(path);
    // Not restarted on every change, so a folder that keeps changing is still read now and then.
    if (!m_debounce.isActive())
        m_debounce.start();
}

void FolderModel::refreshDirtyFolders()
{
    const QSet<QString> dirty = std::exchange(m_dirty, {});
    for (const QString &path : dirty) {
        if (Node *folder = folderNode(path))
            requestListing(folder);
    }
    settle();
}

void FolderModel::settle()
{
    if (m_selectionDirty) {
        m_selectionDirty = false;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(count() - 1), {SelectedRole});
        emit selectionChanged();
    }
    if (count() != m_shownCount) {
        m_shownCount = count();
        emit countChanged();
    }
    if (isLoading() != m_shownLoading) {
        m_shownLoading = isLoading();
        emit loadingChanged();
    }
}

// ---- types ---------------------------------------------------------------------------------

const FolderModel::TypeInfo &FolderModel::typeInfo(const DirEntry &entry) const
{
    if (entry.isDir) {
        static const TypeInfo folder{tr("Folder"), QStringLiteral("folder"),
                                     QStringLiteral("folder")};
        return folder;
    }

    // Keyed by everything after the first dot, so "x.tar.gz" and "y.gz" can differ; names
    // without a dot are looked up by name ("Makefile").
    const qsizetype dot = entry.name.indexOf(QLatin1Char('.'), 1);
    const QString key = dot > 0 ? entry.name.mid(dot + 1).toLower()
                                : QLatin1Char('/') + entry.name;
    auto it = m_typeCache.find(key);
    if (it == m_typeCache.end()) {
        const QMimeType mime = QMimeDatabase().mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension);
        TypeInfo info;
        info.kindText = mime.comment();
        info.category = categoryOf(mime);
        info.iconName = mime.iconName();
        it = m_typeCache.insert(key, info);
    }
    return it.value();
}

} // namespace Riley
