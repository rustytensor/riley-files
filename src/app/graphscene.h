// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "graphbuilder.h"

#include <QColor>
#include <QList>
#include <QPointer>
#include <QVariantMap>
#include <QVector3D>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QtQuick3D/qquick3dgeometry.h>
#include <QtQuick3D/qquick3dinstancing.h>

// What the graph view draws in 3D, from the graph a GraphBuilder built: one sphere per node, the
// lines that join each node to its parent folder, and the stars behind them. The QML scene only
// places a camera and lights; where each node is, how large and in what colour, is decided here.

// One sphere per node, as an instancing table for a Model with the "#Sphere" mesh (100 units
// across). A folder is larger the more files are below it; a file is small. Folders are drawn
// in folderColor, files in the colour kindColors gives their kind (image, audio, video, text,
// code, document, archive, file: Theme.categoryColors).
class GraphInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Riley::GraphBuilder *builder READ builder WRITE setBuilder NOTIFY builderChanged)
    Q_PROPERTY(QColor folderColor READ folderColor WRITE setFolderColor NOTIFY coloursChanged)
    Q_PROPERTY(QVariantMap kindColors READ kindColors WRITE setKindColors NOTIFY coloursChanged)
    // How far the farthest node is from the folder walked (at the centre), in scene units.
    Q_PROPERTY(float radius READ radius NOTIFY graphChanged)
    // Goes up by one each time the nodes change, for bindings that place things on them.
    Q_PROPERTY(int revision READ revision NOTIFY graphChanged)

public:
    // Scene units for one unit of the layout.
    static constexpr float Scale = 60.0f;

    explicit GraphInstancing(QQuick3DObject *parent = nullptr);

    Riley::GraphBuilder *builder() const { return m_builder; }
    void setBuilder(Riley::GraphBuilder *builder);
    QColor folderColor() const { return m_folderColor; }
    void setFolderColor(const QColor &colour);
    QVariantMap kindColors() const { return m_kindColors; }
    void setKindColors(const QVariantMap &colours);
    float radius() const { return m_radius; }
    int revision() const { return m_revision; }

    // Where a node is in the scene, and how wide its sphere is; the centre and 0 for no node.
    Q_INVOKABLE QVector3D positionOf(int node) const;
    Q_INVOKABLE float sizeOf(int node) const;
    // The node under the point (x, y) of `view` (the View3D that draws them), or -1: of the nodes
    // whose sphere covers the point (with a few pixels to spare), the one whose centre is
    // nearest to it. A small node in front of a large one is found where it is drawn.
    Q_INVOKABLE int nodeAt(QObject *view, qreal x, qreal y) const;

    // A node's kind, as its colour says: "folder", or the kind of file it is.
    QString kindOf(int node) const;

signals:
    void builderChanged();
    void coloursChanged();
    void graphChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    void rebuild();

    QPointer<Riley::GraphBuilder> m_builder;
    QColor m_folderColor;
    QVariantMap m_kindColors;
    QList<QVector3D> m_positions;
    QList<float> m_sizes;
    QStringList m_kinds;
    float m_radius = 0.0f;
    int m_revision = 0;
    QByteArray m_table;
    bool m_tableDirty = true;
};

// The thin lines from each node to its parent folder.
class GraphEdges : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(GraphInstancing *nodes READ nodes WRITE setNodes NOTIFY nodesChanged)

public:
    explicit GraphEdges(QQuick3DObject *parent = nullptr);

    GraphInstancing *nodes() const { return m_nodes; }
    void setNodes(GraphInstancing *nodes);

signals:
    void nodesChanged();

private:
    void rebuild();

    QPointer<GraphInstancing> m_nodes;
};

// The stars: points scattered over a shell far behind the graph, the same ones every time,
// each in `colour` at its own brightness.
class GraphStars : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(float radius READ radius WRITE setRadius NOTIFY starsChanged)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY starsChanged)
    Q_PROPERTY(QColor colour READ colour WRITE setColour NOTIFY starsChanged)

public:
    explicit GraphStars(QQuick3DObject *parent = nullptr);

    float radius() const { return m_radius; }
    void setRadius(float radius);
    int count() const { return m_count; }
    void setCount(int count);
    QColor colour() const { return m_colour; }
    void setColour(const QColor &colour);

signals:
    void starsChanged();

private:
    void rebuild();

    float m_radius = 3000.0f;
    int m_count = 900;
    QColor m_colour;
};
