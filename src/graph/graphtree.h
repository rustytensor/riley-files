// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QtGlobal>

#include <atomic>

namespace Riley {

// One folder or file in the graph.
struct GraphNode
{
    QString name;        // the file name alone; for the folder walked, its own name
    QString path;        // absolute
    int parent = -1;     // index of the folder it is in; -1 for the folder walked
    int depth = 0;       // 0 for the folder walked, 1 for what is right in it, and so on
    bool isDir = false;
    // Files anywhere below a folder, counted even when the graph shows folders only; 0 for a file.
    qint64 filesBelow = 0;
    QList<int> children; // folders first, then files, by name the way a person reads them
};

// A folder and everything below it, as nodes joined to their parent folder.
//
// The walk never follows a symbolic link to a folder (the link is shown as a file), never goes
// into a folder on another filesystem (the folder is shown, not what is in it), and skips hidden
// files and folders unless asked for. When the tree holds more than MaxItems files and folders
// below the folder walked, only the folders are kept, and fileCount() says how many files there
// were. Node 0 is the folder walked; a node's parent always comes before it.
class RILEYGRAPH_EXPORT GraphTree
{
public:
    // The most files and folders shown with their files; above it, folders only.
    static constexpr int MaxItems = 2600;

    GraphTree() = default;

    // Walks root. Plain and blocking: call it from a worker thread. When stop is given and
    // becomes true, the walk ends early and stopped() is true.
    static GraphTree walk(const QString &root, bool showHidden, const std::atomic<bool> *stop = nullptr);

    bool isEmpty() const { return m_nodes.isEmpty(); }
    int size() const { return int(m_nodes.size()); }
    const GraphNode &node(int index) const { return m_nodes.at(index); }
    const QList<GraphNode> &nodes() const { return m_nodes; }
    // Each node but the first joined to its parent folder: (node, parent).
    QList<QPair<int, int>> edges() const;
    // The parent of each node, -1 for the first.
    QList<int> parents() const;
    // The node for an absolute path; -1 when it is not in the graph.
    int indexOf(const QString &path) const;

    QString root() const { return m_nodes.isEmpty() ? QString() : m_nodes.first().path; }
    // True when the tree was too large and only its folders are nodes.
    bool foldersOnly() const { return m_foldersOnly; }
    // Every file below the folder walked, shown or not.
    qint64 fileCount() const { return m_fileCount; }
    // Every folder below the folder walked.
    qint64 folderCount() const { return m_folderCount; }
    bool stopped() const { return m_stopped; }

private:
    QList<GraphNode> m_nodes;
    bool m_foldersOnly = false;
    bool m_stopped = false;
    qint64 m_fileCount = 0;
    qint64 m_folderCount = 0;
};

} // namespace Riley
