// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphtree.h"

#include "direntry.h"
#include "entryorder.h"

#include <QDir>
#include <QFile>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <vector>

namespace Riley {

namespace {

// One thing read from a folder, before it is sorted.
struct Found
{
    DirEntry entry;
    bool descend = false; // a folder on the same filesystem
};

bool isDotOrDotDot(const char *name)
{
    return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

QString childPath(const QString &folder, const QString &name)
{
    return folder == QLatin1String("/") ? QLatin1Char('/') + name : folder + QLatin1Char('/') + name;
}

} // namespace

GraphTree GraphTree::walk(const QString &rootPath, bool showHidden, const std::atomic<bool> *stop)
{
    GraphTree tree;
    const auto stopping = [stop] { return stop && stop->load(std::memory_order_relaxed); };

    const QString root = QDir::cleanPath(QDir(rootPath).absolutePath());
    struct stat rootInfo;
    if (::stat(QFile::encodeName(root).constData(), &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode))
        return tree;
    const dev_t device = rootInfo.st_dev;

    GraphNode first;
    first.name = root == QLatin1String("/") ? root : QDir(root).dirName();
    first.path = root;
    first.isDir = true;
    tree.m_nodes.append(first);

    // Files right in each node, counted whether or not they are kept as nodes.
    std::vector<qint64> directFiles(1, 0);
    qint64 items = 0;
    const EntryOrder order;

    // Folders still to read. A folder's children are added together, right after it is read,
    // so a parent always comes before its children.
    std::vector<int> pending{0};
    while (!pending.empty()) {
        if (stopping()) {
            tree.m_stopped = true;
            break;
        }
        const int folder = pending.back();
        pending.pop_back();
        const QString folderPath = tree.m_nodes.at(folder).path;
        DIR *dir = ::opendir(QFile::encodeName(folderPath).constData());
        if (!dir)
            continue;
        const int fd = ::dirfd(dir);
        QList<Found> found;
        int seen = 0;
        while (const dirent *item = ::readdir(dir)) {
            if ((++seen & 255) == 0 && stopping())
                break;
            const char *name = item->d_name;
            if (isDotOrDotDot(name) || (name[0] == '.' && !showHidden))
                continue;
            bool isDir = false;
            bool descend = false;
            if (item->d_type == DT_DIR || item->d_type == DT_UNKNOWN) {
                struct stat info;
                if (::fstatat(fd, name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(info.st_mode)) {
                    isDir = true;
                    descend = info.st_dev == device;
                }
            }
            Found one;
            one.entry.name = QFile::decodeName(name);
            one.entry.isDir = isDir;
            one.entry.hidden = name[0] == '.';
            one.descend = descend;
            found.append(std::move(one));
        }
        ::closedir(dir);
        if (stopping()) {
            tree.m_stopped = true;
            break;
        }

        std::sort(found.begin(), found.end(),
                  [&order](const Found &a, const Found &b) { return order.lessThan(a.entry, b.entry); });

        const int depth = tree.m_nodes.at(folder).depth + 1;
        std::vector<int> subfolders;
        for (Found &one : found) {
            ++items;
            if (one.entry.isDir)
                ++tree.m_folderCount;
            else {
                ++tree.m_fileCount;
                ++directFiles[folder];
            }
            if (items > MaxItems)
                tree.m_foldersOnly = true;
            // Past the limit files are only counted; those kept before it are dropped below.
            if (!one.entry.isDir && tree.m_foldersOnly)
                continue;
            GraphNode node;
            node.name = std::move(one.entry.name);
            node.path = childPath(folderPath, node.name);
            node.parent = folder;
            node.depth = depth;
            node.isDir = one.entry.isDir;
            const int index = int(tree.m_nodes.size());
            tree.m_nodes.append(std::move(node));
            tree.m_nodes[folder].children.append(index);
            directFiles.push_back(0);
            if (one.descend)
                subfolders.push_back(index);
        }
        // Read last-in first-out: push in reverse so the first folder is read first.
        for (auto it = subfolders.rbegin(); it != subfolders.rend(); ++it)
            pending.push_back(*it);
    }

    // Files below each folder: children always come after their parent, so one pass from the end
    // adds each folder's count into its parent's.
    for (int i = 0; i < tree.size(); ++i)
        tree.m_nodes[i].filesBelow = tree.m_nodes.at(i).isDir ? directFiles[i] : 0;
    for (int i = tree.size() - 1; i > 0; --i) {
        if (tree.m_nodes.at(i).isDir)
            tree.m_nodes[tree.m_nodes.at(i).parent].filesBelow += tree.m_nodes.at(i).filesBelow;
    }

    if (tree.m_foldersOnly) {
        // Keep the folders, in the same order, with their indices renumbered.
        std::vector<int> renumbered(tree.m_nodes.size(), -1);
        QList<GraphNode> folders;
        for (int i = 0; i < tree.size(); ++i) {
            if (!tree.m_nodes.at(i).isDir)
                continue;
            renumbered[i] = int(folders.size());
            folders.append(tree.m_nodes.at(i));
        }
        for (GraphNode &node : folders) {
            if (node.parent >= 0)
                node.parent = renumbered[node.parent];
            QList<int> children;
            for (int child : std::as_const(node.children)) {
                if (renumbered[child] >= 0)
                    children.append(renumbered[child]);
            }
            node.children = std::move(children);
        }
        tree.m_nodes = std::move(folders);
    }
    return tree;
}

QList<QPair<int, int>> GraphTree::edges() const
{
    QList<QPair<int, int>> list;
    list.reserve(qMax(0, size() - 1));
    for (int i = 1; i < size(); ++i)
        list.append({i, m_nodes.at(i).parent});
    return list;
}

QList<int> GraphTree::parents() const
{
    QList<int> list;
    list.reserve(size());
    for (const GraphNode &node : m_nodes)
        list.append(node.parent);
    return list;
}

int GraphTree::indexOf(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (int i = 0; i < size(); ++i) {
        if (m_nodes.at(i).path == clean)
            return i;
    }
    return -1;
}

} // namespace Riley
