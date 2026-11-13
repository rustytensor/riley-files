// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphbuilder.h"

#include "graphgrowth.h"
#include "graphlook.h"
#include "sitelayout.h"

#include <QDir>
#include <QLocale>
#include <QMetaObject>
#include <QMutexLocker>

namespace Riley {

GraphBuilder::GraphBuilder(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

GraphBuilder::~GraphBuilder()
{
    if (m_stop)
        m_stop->store(true);
    m_pool.waitForDone();
}

void GraphBuilder::start(const QString &folder, bool showHidden)
{
    if (m_stop)
        m_stop->store(true);
    ++m_generation;
    m_stop = std::make_shared<std::atomic<bool>>(false);
    m_folder = QDir::cleanPath(folder);
    emit started();
    if (m_folder.isEmpty()) {
        setRunning(false);
        return;
    }
    setRunning(true);
    const quint64 generation = m_generation;
    const QString root = m_folder;
    const auto stop = m_stop;
    m_pool.start([this, generation, root, showHidden, stop] { run(generation, root, showHidden, stop); });
}

void GraphBuilder::stop()
{
    if (m_stop)
        m_stop->store(true);
    ++m_generation;
    setRunning(false);
}

void GraphBuilder::clear()
{
    stop();
    m_tree = GraphTree();
    m_positions.clear();
    m_sitePositions.clear();
    m_sizes.clear();
    m_groups.clear();
    m_born.clear();
    emit built();
}

void GraphBuilder::run(quint64 generation, QString folder, bool showHidden, std::shared_ptr<std::atomic<bool>> stop)
{
    noteThread();
    Built made;
    made.tree = GraphTree::walk(folder, showHidden, stop.get());
    if (stop->load())
        return;
    made.positions = ForceLayout::layOut(made.tree, stop.get());
    if (stop->load())
        return;
    made.sitePositions = SiteLayout::layOut(made.tree, stop.get());
    if (stop->load())
        return;
    made.sizes = SiteLayout::sizes(made.tree);
    made.groups = GraphLook::groupsOf(made.tree);
    made.born = GraphHistory::bornTimes(made.tree, stop.get());
    if (stop->load())
        return;
    QMetaObject::invokeMethod(
        this, [this, generation, made = std::move(made)]() mutable { receive(generation, std::move(made)); },
        Qt::QueuedConnection);
}

void GraphBuilder::receive(quint64 generation, Built made)
{
    if (generation != m_generation)
        return;
    m_tree = std::move(made.tree);
    m_positions = std::move(made.positions);
    m_sitePositions = std::move(made.sitePositions);
    m_sizes = std::move(made.sizes);
    m_groups = std::move(made.groups);
    m_born = std::move(made.born);
    setRunning(false);
    emit built();
}

void GraphBuilder::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void GraphBuilder::noteThread()
{
    QMutexLocker locker(&m_threadsLock);
    m_threads.insert(QThread::currentThreadId());
}

QSet<Qt::HANDLE> GraphBuilder::threads() const
{
    QMutexLocker locker(&m_threadsLock);
    return m_threads;
}

QString GraphBuilder::foldersOnlyNote(qint64 files)
{
    return tr("Showing folders only (%1 files).").arg(QLocale().toString(files));
}

QString GraphBuilder::note() const
{
    if (!m_tree.foldersOnly())
        return {};
    return foldersOnlyNote(m_tree.fileCount());
}

QString GraphBuilder::label(int node) const
{
    if (node < 0 || node >= m_tree.size())
        return {};
    const GraphNode &item = m_tree.node(node);
    if (!item.isDir)
        return item.name;
    if (item.filesBelow == 1)
        return tr("%1 · 1 file").arg(item.name);
    return tr("%1 · %2 files").arg(item.name, QLocale().toString(item.filesBelow));
}

int GraphBuilder::parentOf(int node) const
{
    if (node < 0 || node >= m_tree.size())
        return -1;
    return m_tree.node(node).parent;
}

int GraphBuilder::firstChildOf(int node) const
{
    if (node < 0 || node >= m_tree.size() || m_tree.node(node).children.isEmpty())
        return -1;
    return m_tree.node(node).children.first();
}

int GraphBuilder::nextSiblingOf(int node) const
{
    const int parent = parentOf(node);
    if (parent < 0)
        return -1;
    const QList<int> &siblings = m_tree.node(parent).children;
    const qsizetype at = siblings.indexOf(node);
    return at >= 0 && at + 1 < siblings.size() ? siblings.at(at + 1) : -1;
}

int GraphBuilder::previousSiblingOf(int node) const
{
    const int parent = parentOf(node);
    if (parent < 0)
        return -1;
    const QList<int> &siblings = m_tree.node(parent).children;
    const qsizetype at = siblings.indexOf(node);
    return at > 0 ? siblings.at(at - 1) : -1;
}

QString GraphBuilder::pathOf(int node) const
{
    if (node < 0 || node >= m_tree.size())
        return {};
    return m_tree.node(node).path;
}

bool GraphBuilder::isFolder(int node) const
{
    return node >= 0 && node < m_tree.size() && m_tree.node(node).isDir;
}

} // namespace Riley
