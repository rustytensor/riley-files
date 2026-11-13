// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphscene.h"

#include "details.h"

#include <QMetaObject>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QtMath>

#include <cmath>
#include <limits>

namespace {

// Sphere widths, in units of the layout (whose nodes are at least 0.6 apart).
constexpr float fileSize = 0.22f;
constexpr float folderSize = 0.32f;
constexpr float folderGrowth = 0.07f; // more per doubling of the files below
constexpr float largestSize = 1.1f;
// The "#Sphere" mesh is 100 scene units across.
constexpr float meshSize = 100.0f;
// Pixels around a sphere that still count as on it.
constexpr qreal pickSlack = 4.0;

} // namespace

GraphInstancing::GraphInstancing(QQuick3DObject *parent)
    : QQuick3DInstancing(parent)
{
}

void GraphInstancing::setBuilder(Riley::GraphBuilder *builder)
{
    if (builder == m_builder)
        return;
    if (m_builder)
        disconnect(m_builder, nullptr, this, nullptr);
    m_builder = builder;
    if (m_builder)
        connect(m_builder, &Riley::GraphBuilder::built, this, &GraphInstancing::rebuild);
    emit builderChanged();
    rebuild();
}

void GraphInstancing::setFolderColor(const QColor &colour)
{
    if (colour == m_folderColor)
        return;
    m_folderColor = colour;
    m_tableDirty = true;
    markDirty();
    emit coloursChanged();
}

void GraphInstancing::setKindColors(const QVariantMap &colours)
{
    if (colours == m_kindColors)
        return;
    m_kindColors = colours;
    m_tableDirty = true;
    markDirty();
    emit coloursChanged();
}

void GraphInstancing::rebuild()
{
    m_positions.clear();
    m_sizes.clear();
    m_kinds.clear();
    m_radius = 0.0f;
    if (m_builder) {
        const Riley::GraphTree &tree = m_builder->tree();
        const QList<Riley::GraphPoint> &points = m_builder->positions();
        const int count = int(qMin<qsizetype>(tree.size(), points.size()));
        m_positions.reserve(count);
        m_sizes.reserve(count);
        m_kinds.reserve(count);
        const QMimeDatabase mimes;
        for (int i = 0; i < count; ++i) {
            const Riley::GraphPoint &p = points.at(i);
            const QVector3D at(float(p.x) * Scale, float(p.y) * Scale, float(p.z) * Scale);
            m_positions.append(at);
            m_radius = qMax(m_radius, at.length());
            const Riley::GraphNode &node = tree.node(i);
            if (node.isDir) {
                const float grown = folderSize + folderGrowth * float(std::log2(1.0 + double(node.filesBelow)));
                m_sizes.append(qMin(grown, largestSize) * Scale);
                m_kinds.append(QStringLiteral("folder"));
            } else {
                m_sizes.append(fileSize * Scale);
                // By the name alone: the graph never opens the files it shows.
                const QMimeType mime = mimes.mimeTypeForFile(node.name, QMimeDatabase::MatchExtension);
                m_kinds.append(FileDetails::categoryFor(mime.name(), false));
            }
        }
    }
    ++m_revision;
    m_tableDirty = true;
    markDirty();
    emit graphChanged();
}

QVector3D GraphInstancing::positionOf(int node) const
{
    return node >= 0 && node < m_positions.size() ? m_positions.at(node) : QVector3D();
}

float GraphInstancing::sizeOf(int node) const
{
    return node >= 0 && node < m_sizes.size() ? m_sizes.at(node) : 0.0f;
}

QString GraphInstancing::kindOf(int node) const
{
    return node >= 0 && node < m_kinds.size() ? m_kinds.at(node) : QString();
}

int GraphInstancing::nodeAt(QObject *view, qreal x, qreal y) const
{
    if (!view)
        return -1;
    int found = -1;
    qreal nearest = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_positions.size(); ++i) {
        QVector3D centre;
        QVector3D edge;
        const QVector3D at = m_positions.at(i);
        if (!QMetaObject::invokeMethod(view, "mapFrom3DScene", Q_RETURN_ARG(QVector3D, centre), Q_ARG(QVector3D, at))
            || !QMetaObject::invokeMethod(view, "mapFrom3DScene", Q_RETURN_ARG(QVector3D, edge),
                                          Q_ARG(QVector3D, at + QVector3D(0.0f, m_sizes.at(i) / 2.0f, 0.0f))))
            return -1;
        if (centre.z() <= 0.0f) // behind the camera
            continue;
        const qreal reach = qHypot(edge.x() - centre.x(), edge.y() - centre.y()) + pickSlack;
        const qreal distance = qHypot(x - centre.x(), y - centre.y());
        if (distance <= reach && distance < nearest) {
            nearest = distance;
            found = i;
        }
    }
    return found;
}

QByteArray GraphInstancing::getInstanceBuffer(int *instanceCount)
{
    if (m_tableDirty) {
        m_table.clear();
        m_table.reserve(m_positions.size() * qsizetype(sizeof(InstanceTableEntry)));
        const QColor fileColour = m_kindColors.value(QStringLiteral("file")).value<QColor>();
        for (int i = 0; i < m_positions.size(); ++i) {
            const float scale = m_sizes.at(i) / meshSize;
            QColor colour = m_folderColor;
            if (m_kinds.at(i) != QLatin1String("folder")) {
                const QVariant given = m_kindColors.value(m_kinds.at(i));
                colour = given.isValid() ? given.value<QColor>() : fileColour;
            }
            const InstanceTableEntry entry = calculateTableEntry(m_positions.at(i), QVector3D(scale, scale, scale),
                                                                 QVector3D(), colour);
            m_table.append(reinterpret_cast<const char *>(&entry), sizeof(entry));
        }
        m_tableDirty = false;
    }
    if (instanceCount)
        *instanceCount = int(m_positions.size());
    return m_table;
}

GraphEdges::GraphEdges(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void GraphEdges::setNodes(GraphInstancing *nodes)
{
    if (nodes == m_nodes)
        return;
    if (m_nodes)
        disconnect(m_nodes, nullptr, this, nullptr);
    m_nodes = nodes;
    if (m_nodes)
        connect(m_nodes, &GraphInstancing::graphChanged, this, &GraphEdges::rebuild);
    emit nodesChanged();
    rebuild();
}

void GraphEdges::rebuild()
{
    clear();
    QByteArray vertices;
    QVector3D low;
    QVector3D high;
    if (m_nodes && m_nodes->builder()) {
        const Riley::GraphTree &tree = m_nodes->builder()->tree();
        const auto edges = tree.edges();
        vertices.reserve(edges.size() * 2 * 3 * qsizetype(sizeof(float)));
        for (const auto &edge : edges) {
            for (const int end : {edge.first, edge.second}) {
                const QVector3D at = m_nodes->positionOf(end);
                for (const float value : {at.x(), at.y(), at.z()})
                    vertices.append(reinterpret_cast<const char *>(&value), sizeof(value));
                for (int axis = 0; axis < 3; ++axis) {
                    low[axis] = qMin(low[axis], at[axis]);
                    high[axis] = qMax(high[axis], at[axis]);
                }
            }
        }
    }
    setVertexData(vertices);
    setStride(3 * sizeof(float));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    setBounds(low, high);
    update();
}

GraphStars::GraphStars(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void GraphStars::setRadius(float radius)
{
    if (qFuzzyCompare(radius, m_radius))
        return;
    m_radius = radius;
    rebuild();
    emit starsChanged();
}

void GraphStars::setCount(int count)
{
    if (count == m_count)
        return;
    m_count = qMax(0, count);
    rebuild();
    emit starsChanged();
}

void GraphStars::setColour(const QColor &colour)
{
    if (colour == m_colour)
        return;
    m_colour = colour;
    rebuild();
    emit starsChanged();
}

void GraphStars::rebuild()
{
    clear();
    // The same sky every time.
    QRandomGenerator random(20261113);
    QByteArray vertices;
    vertices.reserve(qsizetype(m_count) * 7 * qsizetype(sizeof(float)));
    const auto put = [&vertices](float value) { vertices.append(reinterpret_cast<const char *>(&value), sizeof(value)); };
    for (int i = 0; i < m_count; ++i) {
        // A direction spread evenly over the sphere, and a distance on the shell.
        const float z = float(random.generateDouble() * 2.0 - 1.0);
        const float angle = float(random.generateDouble() * 2.0 * M_PI);
        const float across = std::sqrt(qMax(0.0f, 1.0f - z * z));
        const float distance = m_radius * float(1.0 + random.generateDouble() * 0.5);
        put(across * std::cos(angle) * distance);
        put(across * std::sin(angle) * distance);
        put(z * distance);
        // Most stars faint, a few bright.
        const float brightness = float(0.25 + 0.75 * std::pow(random.generateDouble(), 3.0));
        put(float(m_colour.redF()) * brightness);
        put(float(m_colour.greenF()) * brightness);
        put(float(m_colour.blueF()) * brightness);
        put(1.0f);
    }
    setVertexData(vertices);
    setStride(7 * sizeof(float));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Points);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 3 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
    const float far = m_radius * 1.5f;
    setBounds(QVector3D(-far, -far, -far), QVector3D(far, far, far));
    update();
}
