// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include "forcelayout.h"
#include "graphlook.h"

#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <vector>

namespace Riley {

class GraphFlash;
class GraphPulses;
class GraphSky;
class GrowthReplay;

// One corner of a triangle the view draws: a point in pixels and a colour premultiplied by its
// alpha. Laid out as the scene graph's coloured 2D point (two floats, four bytes), so a frame is
// copied to the screen as it is.
struct GraphVertex
{
    float x;
    float y;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

// One frame of the graph as triangles, the way servorra.com draws its graph on a 2D canvas: the
// sky (three layers of stars and the shooting stars), the tree's edges, the nodes (far band first,
// one batch per colour and band), a ring round the selected node, and the pulses (a soft glow
// under a bright dot). Every shape is a fan or a strip with a one-pixel soft edge, so it is
// smooth without multisampling. Also the labels to write and where each node landed, for the
// mouse. Qt Core only, so it can be checked without a window.
class RILEYGRAPH_EXPORT GraphFrame
{
public:
    // Half the width of the soft edge round every shape, in pixels.
    static constexpr double Feather = 0.5;
    // Pixels round a node that still count as on it.
    static constexpr double PickSlack = 4;

    // The graph drawn: its layout in [-1,1], each node's size (1..255), group and name, and the
    // tree's edges as (node, parent).
    void setGraph(const QList<GraphPoint> &points, const QList<int> &sizes, const QList<int> &groups,
                  const QStringList &names, const QList<QPair<int, int>> &edges);
    int nodeCount() const { return int(m_points.size()); }

    struct View
    {
        double width = 0;
        double height = 0;
        double rotY = 0;
        double rotX = 0;
        double zoom = 1;
        double timeMs = 0;  // for the twinkling
        int selected = -1;  // ringed; -1 for none
        // The node the tooltip is about (-1 for none) and the tooltip's size in pixels: the
        // tooltip is placed beside the node, and labels keep clear of it.
        int tooltipNode = -1;
        double tooltipWidth = 0;
        double tooltipHeight = 0;
        // Whether the labels keep clear of the tooltip: its node's label is not drawn, nor any
        // under it. True while the mouse is on the node; the tooltip of the node the keys are on
        // leaves the labels as they are.
        bool tooltipClearsLabels = true;
    };

    // Makes the frame. Any of flash, pulses, sky and growth may be null (no flashes, no pulses,
    // no sky, everything shown). measure gives a label's width in pixels (an estimate when empty).
    void prepare(const View &view, const GraphFlash *flash, const GraphPulses *pulses, const GraphSky *sky,
                 const GrowthReplay *growth, const std::function<double(const QString &)> &measure = {});

    const std::vector<GraphVertex> &vertices() const { return m_vertices; }
    const std::vector<quint32> &indices() const { return m_indices; }
    const QList<GraphLook::Label> &labels() const { return m_labels; }
    // Where the tooltip goes in the last frame; empty when there is none.
    GraphLook::Box tooltip() const { return m_tooltip; }
    // Nodes drawn in the last frame (those "Watch it grow" has reached).
    int drawnNodes() const { return m_drawn; }
    int drawnEdges() const { return m_drawnEdges; }
    int drawnPulses() const { return m_drawnPulses; }
    int drawnStars() const { return m_drawnStars; }

    // Whether a node was drawn in the last frame.
    bool drawn(int node) const { return node >= 0 && size_t(node) < m_shown.size() && m_shown[size_t(node)]; }
    // Where a node was drawn in the last frame, and how large; (-1, -1) and 0 for no node.
    QPointF pointOf(int node) const;
    double radiusOf(int node) const;
    // The node drawn under (x, y) in the last frame, or -1: of those whose disc (with a few
    // pixels to spare) covers the point, the one whose centre is nearest.
    int nodeAt(double x, double y) const;

    // The shapes, for the frame and for tests: a disc of radius r and a ring from inner to outer,
    // both with soft edges; a line of a given width whose alpha runs from alphaA at a to alphaB
    // at b.
    void disc(double x, double y, double r, const GraphRgb &colour, double alpha);
    void ring(double x, double y, double inner, double outer, const GraphRgb &colour, double alpha);
    void line(double ax, double ay, double bx, double by, double width, const GraphRgb &colour, double alphaA,
              double alphaB);
    void clear();

private:
    quint32 vertex(double x, double y, const GraphRgb &colour, double alpha);
    void triangle(quint32 a, quint32 b, quint32 c);

    QList<GraphPoint> m_points;
    QList<int> m_sizes;
    QList<int> m_groups;
    QStringList m_names;
    QList<QPair<int, int>> m_edges;

    QList<GraphLook::Projected> m_projected;
    std::vector<double> m_radii;
    std::vector<char> m_shown;
    std::vector<GraphVertex> m_vertices;
    std::vector<quint32> m_indices;
    QList<GraphLook::Label> m_labels;
    GraphLook::Box m_tooltip;
    int m_drawn = 0;
    int m_drawnEdges = 0;
    int m_drawnPulses = 0;
    int m_drawnStars = 0;
};

} // namespace Riley
