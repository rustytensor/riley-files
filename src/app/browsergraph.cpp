// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The graph view: the folder shown and everything below it as nodes in 3D (riley-graph builds
// it, off the UI thread). The keys move through it as through a tree: Down to the first child,
// Up to the parent, Left and Right between siblings; Return opens a folder in the list view (a
// file with its application), Escape goes back to the list, and G starts "Watch it grow".

#include "browser.h"

#include "graphlook.h"

#include <QColor>

#include <QQuickWindow>
#include <QSGRendererInterface>

using namespace Qt::StringLiterals;

void Browser::startGraph()
{
    m_graphPath = folder();
    m_graphHover = -1;
    m_graphNode = -1;
    emit graphNodeChanged();
    m_graph->start(folder(), showHidden());
}

void Browser::setGraphNode(int node)
{
    if (node < -1 || node >= m_graph->count())
        node = -1;
    const QString path = m_graph->pathOf(node);
    if (node == m_graphNode && path == m_graphPath)
        return;
    m_graphNode = node;
    if (!path.isEmpty())
        m_graphPath = path;
    emit graphNodeChanged();
}

QString Browser::graphLabel() const
{
    const int node = graphLabelNode();
    return node >= 0 ? m_graph->label(node) : QString();
}

void Browser::hoverGraphNode(int node)
{
    if (node < 0 || node >= m_graph->count())
        node = -1;
    if (node == m_graphHover)
        return;
    m_graphHover = node;
    emit graphNodeChanged();
}

void Browser::clickGraphNode(int node)
{
    if (node >= 0 && node < m_graph->count())
        setGraphNode(node);
}

void Browser::openGraphNode(int node)
{
    const QString path = m_graph->pathOf(node);
    if (path.isEmpty())
        return;
    if (m_graph->isFolder(node)) {
        // The folder is shown in the list view, as a new step in the history.
        setView(List);
        open(path);
        return;
    }
    setGraphNode(node);
    openFiles({path});
}

bool Browser::graphCanDraw(QObject *window) const
{
    if (qEnvironmentVariableIsSet("RILEY_FILES_NO_3D"))
        return false;
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow)
        return false;
    const QSGRendererInterface *renderer = quickWindow->rendererInterface();
    if (!renderer)
        return false;
    const QSGRendererInterface::GraphicsApi api = renderer->graphicsApi();
    return QSGRendererInterface::isApiRhiBased(api) && api != QSGRendererInterface::Null;
}

QVariantMap Browser::graphPalette() const
{
    const auto colour = [](const Riley::GraphRgb &rgb) { return QVariant::fromValue(QColor(rgb.r, rgb.g, rgb.b)); };
    return {{u"background"_s, colour(Riley::GraphLook::Background)},
            {u"text"_s, colour(Riley::GraphLook::LabelBright)},
            {u"dim"_s, colour(Riley::GraphLook::LabelDim)},
            {u"accent"_s, colour(Riley::GraphLook::Teal)},
            {u"panel"_s, colour(Riley::GraphLook::Background)}};
}

bool Browser::graphKey(int key)
{
    if (key == Qt::Key_Escape) {
        setView(List);
        return true;
    }
    if (key == Qt::Key_G) {
        emit graphGrowAsked();
        return true;
    }
    if (m_graphNode < 0)
        return key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right
               || key == Qt::Key_Return || key == Qt::Key_Enter;
    int next = -1;
    switch (key) {
    case Qt::Key_Down:
        next = m_graph->firstChildOf(m_graphNode);
        break;
    case Qt::Key_Up:
        next = m_graph->parentOf(m_graphNode);
        break;
    case Qt::Key_Right:
        next = m_graph->nextSiblingOf(m_graphNode);
        break;
    case Qt::Key_Left:
        next = m_graph->previousSiblingOf(m_graphNode);
        break;
    case Qt::Key_Home:
        next = 0;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        openGraphNode(m_graphNode);
        return true;
    default:
        return false;
    }
    // At the end of a way (no child, no parent, no sibling) the selection stays.
    if (next >= 0) {
        m_graphHover = -1; // the label follows the keys again
        setGraphNode(next);
        emit graphNodeChanged();
    }
    return true;
}
