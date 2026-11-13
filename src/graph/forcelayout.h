// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include <QList>
#include <QtGlobal>

#include <atomic>

namespace Riley {

class GraphTree;

// A place in the graph's space.
struct GraphPoint
{
    double x = 0;
    double y = 0;
    double z = 0;

    friend bool operator==(const GraphPoint &, const GraphPoint &) = default;
};

// Places the nodes of a tree in 3D: every edge is a spring of EdgeLength, every two nodes push
// each other apart (far groups of nodes taken as one, through an octree), and the start comes
// from a fixed seed, so the same tree is always laid out the same way. At the end no two nodes
// are closer than MinimumDistance. The folder walked sits at the origin.
//
// Plain and blocking: call it from a worker thread.
class RILEYGRAPH_EXPORT ForceLayout
{
public:
    static constexpr double EdgeLength = 2.0;
    static constexpr double MinimumDistance = 0.6;
    static constexpr quint64 Seed = 0x52696c6579u;

    // parents: each node's parent, -1 for the first; a parent always comes before its children.
    // When stop is given and becomes true, the layout ends early with what it has.
    static QList<GraphPoint> layOut(const QList<int> &parents, const std::atomic<bool> *stop = nullptr);
    static QList<GraphPoint> layOut(const GraphTree &tree, const std::atomic<bool> *stop = nullptr);

    // The smallest distance between two of the points (0 for fewer than two). For checks.
    static double closestDistance(const QList<GraphPoint> &points);
};

} // namespace Riley
