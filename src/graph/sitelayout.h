// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include "forcelayout.h"

#include <QList>
#include <QtGlobal>

#include <atomic>

namespace Riley {

class GraphTree;

// The layout of servorra.com's graph: every node pushes every other away (far groups taken as one
// through an octree, Barnes-Hut), each tree edge is a spring of weight 1, a weak pull holds all
// near the middle, for 260 steps that shrink as they go, each step's force held to +-1 per axis.
// The start comes from a normal distribution with seed 7, so the same tree always lands the same
// way. At the end the layout is centred and scaled so its largest coordinate is exactly 1.
//
// Plain and blocking: call it from a worker thread.
class RILEYGRAPH_EXPORT SiteLayout
{
public:
    static constexpr int Iterations = 260;
    static constexpr quint64 Seed = 7;
    static constexpr double SpringWeight = 1.0;
    static constexpr double Gravity = 0.02;
    static constexpr double Softening = 0.01;
    // How far a group of nodes must be, against its size, to be taken as one.
    static constexpr double Theta = 0.6;

    // The repulsion for n nodes: 0.004 * sqrt(2411 / n), so small graphs do not collapse.
    static double repulsionFor(int n);
    // The step of iteration it: 0.08 * (1 - it/260) + 0.004.
    static double stepAt(int iteration);

    // Node sizes 1..255 from weights (files below each node): log2(w+1) / log2(max+1) * 255, held
    // to 1..255 and cut to a whole number.
    static QList<int> sizes(const QList<qint64> &weights);
    static QList<int> sizes(const GraphTree &tree);

    // parents: each node's parent, -1 for the first; a parent always comes before its children.
    // When stop is given and becomes true, the layout ends early with what it has, not scaled.
    static QList<GraphPoint> layOut(const QList<int> &parents, const std::atomic<bool> *stop = nullptr);
    static QList<GraphPoint> layOut(const GraphTree &tree, const std::atomic<bool> *stop = nullptr);

    // The start: n points from a normal distribution with seed 7.
    static QList<GraphPoint> start(int n);
    // The push on each point: strength * sum over the others of d / (|d|^2 + 0.01)^1.5, with
    // d = this point - that one; exact sums every pair, else through the octree.
    static QList<GraphPoint> repulsion(const QList<GraphPoint> &points, double strength, bool exact);
};

} // namespace Riley
