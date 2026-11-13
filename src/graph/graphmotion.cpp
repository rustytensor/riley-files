// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphmotion.h"

#include <algorithm>
#include <cmath>

namespace Riley {

void GraphMotion::press()
{
    m_held = true;
    m_idle = 0;
    m_velX = m_velY = 0;
}

void GraphMotion::dragBy(double dx, double dy)
{
    m_idle = 0;
    m_rotY += dx * DragTurnPerPx;
    m_rotX = std::clamp(m_rotX + dy * DragTurnPerPx, -RotXLimit, RotXLimit);
    m_velY = dx * InertiaYPerPx;
    m_velX = dy * InertiaXPerPx;
}

void GraphMotion::release()
{
    m_held = false;
    m_idle = 0;
}

void GraphMotion::wheel(double deltaY)
{
    zoomBy(std::exp(-deltaY * WheelPerUnit));
}

void GraphMotion::zoomBy(double factor)
{
    m_idle = 0;
    m_zoomTarget = std::clamp(m_zoomTarget * factor, ZoomMin, ZoomMax);
}

void GraphMotion::turnTo(double rotY, double rotX)
{
    m_idle = 0;
    m_velX = m_velY = 0;
    m_rotY = rotY;
    m_rotX = std::clamp(rotX, -RotXLimit, RotXLimit);
}

void GraphMotion::step(double dtMs)
{
    const double dt = std::clamp(dtMs, 0.0, LongestStepMs);
    m_zoom += (m_zoomTarget - m_zoom) * (1 - std::exp(-dt / ZoomEaseMs));
    if (m_held)
        return;
    const double frames = dt / FrameMs;
    m_rotY += m_velY * frames;
    m_rotX = std::clamp(m_rotX + m_velX * frames, -RotXLimit, RotXLimit);
    const double keep = std::pow(InertiaKeep, frames);
    m_velY *= keep;
    m_velX *= keep;
    m_idle += dt;
    if (m_idle > IdleDelayMs)
        m_rotY += dt * IdleSpinPerMs;
}

void GraphFlash::raise(int node, double value)
{
    if (node >= 0 && node < m_flash.size())
        m_flash[node] = std::max(m_flash.at(node), value);
}

void GraphFlash::fade(double dtMs)
{
    const double by = std::max(0.0, dtMs) * FadePerMs;
    for (double &f : m_flash) {
        if (f > 0)
            f = std::max(0.0, f - by);
    }
}

bool GraphFlash::any() const
{
    return std::any_of(m_flash.cbegin(), m_flash.cend(), [](double f) { return f > 0; });
}

GraphPulses::GraphPulses(quint32 seed)
    : m_random(seed)
{
}

void GraphPulses::setEdges(const QList<QPair<int, int>> &edges)
{
    m_edges = edges;
    m_pulses.clear();
}

int GraphPulses::capacity() const
{
    return std::min<int>(MostPulses, int(m_edges.size()));
}

int GraphPulses::from(const Pulse &pulse) const
{
    const auto &e = m_edges.at(pulse.edge);
    return pulse.back ? e.second : e.first;
}

int GraphPulses::to(const Pulse &pulse) const
{
    const auto &e = m_edges.at(pulse.edge);
    return pulse.back ? e.first : e.second;
}

QList<int> GraphPulses::step(double dtMs, GraphFlash *flash)
{
    QList<int> reached;
    const double dt = std::max(0.0, dtMs);
    for (qsizetype k = m_pulses.size() - 1; k >= 0; --k) {
        Pulse &p = m_pulses[k];
        p.t += p.v * dt * ProgressPerMs;
        if (p.t >= 1) {
            const int node = to(p);
            reached.prepend(node);
            if (flash)
                flash->raise(node, GraphFlash::OnArrival);
            m_pulses.removeAt(k);
        }
    }
    const int most = capacity();
    if (most == 0)
        return reached;
    std::uniform_int_distribution<int> edge(0, int(m_edges.size()) - 1);
    std::uniform_real_distribution<double> speed(SlowestSpeed, FastestSpeed);
    std::bernoulli_distribution back(0.5);
    while (m_pulses.size() < most) {
        // At most one pulse on an edge, so a small graph's pulses spread over all of it.
        int e = edge(m_random);
        if (m_edges.size() <= MostPulses * 2) {
            for (int tries = 0; tries < int(m_edges.size()); ++tries) {
                const bool used = std::any_of(m_pulses.cbegin(), m_pulses.cend(),
                                              [e](const Pulse &p) { return p.edge == e; });
                if (!used)
                    break;
                e = (e + 1) % int(m_edges.size());
            }
        }
        m_pulses.append({e, 0.0, speed(m_random), back(m_random)});
    }
    return reached;
}

} // namespace Riley
