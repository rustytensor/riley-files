// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include <QList>
#include <QPair>
#include <QtGlobal>

#include <random>

namespace Riley {

// How the graph turns and zooms, as servorra.com's does: it starts turned a little, spins slowly on
// its own after 4 s without input, follows a drag and keeps turning after it with inertia, and
// eases to the zoom the wheel asks for. Times are in milliseconds.
class RILEYGRAPH_EXPORT GraphMotion
{
public:
    static constexpr double StartRotY = 0.6;
    static constexpr double StartRotX = -0.25;
    static constexpr double IdleDelayMs = 4000;
    static constexpr double IdleSpinPerMs = 0.00012;
    static constexpr double DragTurnPerPx = 0.005;
    static constexpr double InertiaYPerPx = 0.0025; // radians per frame for each pixel of the last move
    static constexpr double InertiaXPerPx = 0.0015;
    static constexpr double InertiaKeep = 0.94;     // per frame of FrameMs
    static constexpr double FrameMs = 1000.0 / 60.0;
    static constexpr double RotXLimit = 1.4;
    static constexpr double WheelPerUnit = 0.0015;  // zoom target x exp(-deltaY * 0.0015)
    static constexpr double ZoomMin = 0.5;
    static constexpr double ZoomMax = 7;
    static constexpr double ZoomEaseMs = 140;
    static constexpr double LongestStepMs = 50;

    double rotY() const { return m_rotY; }
    double rotX() const { return m_rotX; }
    double zoom() const { return m_zoom; }
    double zoomTarget() const { return m_zoomTarget; }
    double idleMs() const { return m_idle; }
    bool spinning() const { return !m_held && m_idle > IdleDelayMs; }
    bool held() const { return m_held; }

    // A press on the graph: it stops turning on its own and waits for the drag.
    void press();
    // The pointer moved by (dx, dy) pixels while pressed: turns at once, and sets the inertia.
    void dragBy(double dx, double dy);
    void release();
    // A wheel step (deltaY as a browser gives it: -120 is one notch towards you, zooming in).
    void wheel(double deltaY);
    // Multiplies the zoom target, held within 0.5..7.
    void zoomBy(double factor);
    // Any other input (a key, a click): counts as not idle.
    void touch() { m_idle = 0; }
    // Turns to a given place at once (keys).
    void turnTo(double rotY, double rotX);

    // Moves time on by dt ms (held to 50 ms a step): the zoom eases, the inertia runs out, and
    // after 4 s without input the graph spins.
    void step(double dtMs);

private:
    double m_rotY = StartRotY;
    double m_rotX = StartRotX;
    double m_velY = 0;
    double m_velX = 0;
    double m_zoom = 1;
    double m_zoomTarget = 1;
    double m_idle = 0;
    bool m_held = false;
};

// How much each node lights up: set when a pulse arrives or a node appears, fading by
// dt * 0.0022 a millisecond.
class RILEYGRAPH_EXPORT GraphFlash
{
public:
    static constexpr double FadePerMs = 0.0022;
    static constexpr double OnArrival = 0.7;
    static constexpr double OnAppearing = 1.0;

    void resize(int n) { m_flash = QList<double>(n, 0.0); }
    int size() const { return int(m_flash.size()); }
    double at(int node) const { return node >= 0 && node < m_flash.size() ? m_flash.at(node) : 0.0; }
    // Raises a node's flash to at least value.
    void raise(int node, double value);
    void fade(double dtMs);
    bool any() const;

private:
    QList<double> m_flash;
};

// Signals travelling along the tree's edges, each a soft glow under a bright dot; when one
// reaches its end, that node flashes. At most 90 at once, and never more than there are edges.
class RILEYGRAPH_EXPORT GraphPulses
{
public:
    static constexpr int MostPulses = 90;
    static constexpr double ProgressPerMs = 0.06; // progress += v * dt * 0.06
    static constexpr double SlowestSpeed = 0.004;
    static constexpr double FastestSpeed = 0.010;
    static constexpr double GlowRadius = 4.5;
    static constexpr double GlowAlpha = 0.18;
    static constexpr double DotRadius = 1.6;
    static constexpr double DotAlpha = 0.95;

    struct Pulse
    {
        int edge = 0;
        double t = 0;      // 0 at its start, 1 at its end
        double v = 0;
        bool back = false; // from the edge's second node to its first
    };

    explicit GraphPulses(quint32 seed = 0x5eed);

    // The edges as (a, b) node pairs; drops the pulses there were.
    void setEdges(const QList<QPair<int, int>> &edges);
    int capacity() const;
    const QList<Pulse> &pulses() const { return m_pulses; }
    void clear() { m_pulses.clear(); }
    // Where a pulse starts and ends.
    int from(const Pulse &pulse) const;
    int to(const Pulse &pulse) const;

    // Moves every pulse on by dt ms; each that arrives raises its end node's flash to 0.7 and is
    // gone; then new ones start until there are capacity() of them. The nodes reached, in order.
    QList<int> step(double dtMs, GraphFlash *flash = nullptr);

private:
    QList<QPair<int, int>> m_edges;
    QList<Pulse> m_pulses;
    std::mt19937 m_random;
};

} // namespace Riley
