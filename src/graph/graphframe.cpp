// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphframe.h"

#include "graphgrowth.h"
#include "graphmotion.h"
#include "graphsky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace Riley {

namespace {

// Discs are cut into this many slices at most and at least, more for larger ones.
constexpr int FewestSlices = 6;
constexpr int MostSlices = 32;

struct Circle
{
    std::array<float, MostSlices> c{};
    std::array<float, MostSlices> s{};
};

// The unit circle cut into n slices, worked out once for each n.
const Circle &circle(int n)
{
    static const auto table = [] {
        std::array<Circle, MostSlices + 1> all{};
        for (int k = FewestSlices; k <= MostSlices; ++k) {
            for (int i = 0; i < k; ++i) {
                const double a = 2 * std::numbers::pi * i / k;
                all[k].c[i] = float(std::cos(a));
                all[k].s[i] = float(std::sin(a));
            }
        }
        return all;
    }();
    return table[std::clamp(n, FewestSlices, MostSlices)];
}

int slicesFor(double radius)
{
    return std::clamp(int(radius * 1.6) + FewestSlices, FewestSlices, MostSlices);
}

unsigned char channel(double value)
{
    return static_cast<unsigned char>(std::clamp(int(std::lround(value)), 0, 255));
}

GraphRgb mix(const GraphRgb &from, const GraphRgb &to, double share)
{
    const auto m = [share](int a, int b) { return int(std::lround(a + (b - a) * share)); };
    return {m(from.r, to.r), m(from.g, to.g), m(from.b, to.b)};
}

// A meteor's colour: the white of the stars.
constexpr GraphRgb MeteorColour{0xec, 0xf0, 0xff};

} // namespace

void GraphFrame::setGraph(const QList<GraphPoint> &points, const QList<int> &sizes, const QList<int> &groups,
                          const QStringList &names, const QList<QPair<int, int>> &edges)
{
    m_points = points;
    m_sizes = sizes;
    m_groups = groups;
    m_names = names;
    m_edges = edges;
    m_projected.clear();
    m_radii.clear();
    m_shown.clear();
    m_labels.clear();
    clear();
}

void GraphFrame::clear()
{
    m_vertices.clear();
    m_indices.clear();
}

quint32 GraphFrame::vertex(double x, double y, const GraphRgb &colour, double alpha)
{
    const double a = std::clamp(alpha, 0.0, 1.0);
    m_vertices.push_back({float(x), float(y), channel(colour.r * a), channel(colour.g * a), channel(colour.b * a),
                          channel(a * 255)});
    return quint32(m_vertices.size() - 1);
}

void GraphFrame::triangle(quint32 a, quint32 b, quint32 c)
{
    m_indices.push_back(a);
    m_indices.push_back(b);
    m_indices.push_back(c);
}

void GraphFrame::disc(double x, double y, double r, const GraphRgb &colour, double alpha)
{
    if (r <= 0 || alpha <= 0)
        return;
    // A disc smaller than its soft edge covers less of its pixels: its alpha shrinks to match
    // (a cone of radius r + 0.5 holding as much as a disc of radius r).
    const double inner = std::max(0.0, r - Feather);
    const double outer = r + Feather;
    if (inner <= 0)
        alpha *= std::min(1.0, 3 * r * r / (outer * outer));
    const int n = slicesFor(r);
    const Circle &unit = circle(n);
    const quint32 centre = vertex(x, y, colour, alpha);
    const quint32 first = quint32(m_vertices.size());
    for (int i = 0; i < n; ++i) {
        if (inner > 0)
            vertex(x + unit.c[i] * inner, y + unit.s[i] * inner, colour, alpha);
        vertex(x + unit.c[i] * outer, y + unit.s[i] * outer, colour, 0);
    }
    const quint32 step = inner > 0 ? 2 : 1;
    for (int i = 0; i < n; ++i) {
        const quint32 here = first + quint32(i) * step;
        const quint32 next = first + quint32((i + 1) % n) * step;
        if (inner > 0) {
            triangle(centre, here, next);
            triangle(here, here + 1, next + 1);
            triangle(here, next + 1, next);
        } else {
            triangle(centre, here, next);
        }
    }
}

void GraphFrame::ring(double x, double y, double inner, double outer, const GraphRgb &colour, double alpha)
{
    if (outer <= inner || alpha <= 0)
        return;
    const double radii[4] = {std::max(0.0, inner - Feather), inner + Feather, std::max(inner + Feather, outer - Feather),
                             outer + Feather};
    const double alphas[4] = {0, alpha, alpha, 0};
    const int n = slicesFor(outer);
    const Circle &unit = circle(n);
    const quint32 first = quint32(m_vertices.size());
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 4; ++k)
            vertex(x + unit.c[i] * radii[k], y + unit.s[i] * radii[k], colour, alphas[k]);
    }
    for (int i = 0; i < n; ++i) {
        const quint32 here = first + quint32(i) * 4;
        const quint32 next = first + quint32((i + 1) % n) * 4;
        for (quint32 k = 0; k < 3; ++k) {
            triangle(here + k, here + k + 1, next + k + 1);
            triangle(here + k, next + k + 1, next + k);
        }
    }
}

void GraphFrame::line(double ax, double ay, double bx, double by, double width, const GraphRgb &colour,
                      double alphaA, double alphaB)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = std::hypot(dx, dy);
    if (length <= 0 || (alphaA <= 0 && alphaB <= 0))
        return;
    // A strip as wide as the line and its soft edge, brightest on its spine; the spine's alpha is
    // raised or lowered so the strip holds as much as a line of the width asked for.
    const double half = width / 2 + Feather;
    const double spine = width / half;
    const double nx = -dy / length * half;
    const double ny = dx / length * half;
    const quint32 first = quint32(m_vertices.size());
    vertex(ax - nx, ay - ny, colour, 0);
    vertex(ax, ay, colour, alphaA * spine);
    vertex(ax + nx, ay + ny, colour, 0);
    vertex(bx - nx, by - ny, colour, 0);
    vertex(bx, by, colour, alphaB * spine);
    vertex(bx + nx, by + ny, colour, 0);
    triangle(first, first + 1, first + 4);
    triangle(first, first + 4, first + 3);
    triangle(first + 1, first + 2, first + 5);
    triangle(first + 1, first + 5, first + 4);
}

void GraphFrame::prepare(const View &view, const GraphFlash *flash, const GraphPulses *pulses, const GraphSky *sky,
                         const GrowthReplay *growth, const std::function<double(const QString &)> &measure)
{
    using namespace GraphLook;
    clear();
    m_labels.clear();
    m_drawn = m_drawnEdges = m_drawnPulses = m_drawnStars = 0;
    const int n = int(m_points.size());
    const Frame frame = frameFor(view.width, view.height, view.zoom);

    // The sky: three layers, far first, each shifted by the turn and twinkling in its groups.
    if (sky && sky->tile() > 1) {
        for (int layer = 0; layer < GraphSky::LayerCount; ++layer) {
            const QList<GraphSky::Star> &stars = sky->stars(layer);
            const double base = GraphSky::layerAlpha(layer);
            double twinkles[GraphSky::TwinkleGroups];
            for (int g = 0; g < GraphSky::TwinkleGroups; ++g)
                twinkles[g] = GraphSky::twinkle(view.timeMs, g, layer);
            for (int i = 0; i < stars.size(); ++i) {
                const QPointF at = sky->position(layer, i, view.rotY, view.rotX);
                if (at.x() > view.width + 2 || at.y() > view.height + 2)
                    continue;
                const GraphSky::Star &s = stars.at(i);
                disc(at.x(), at.y(), s.r, GraphSky::tintColour(s.tint),
                     base * twinkles[std::clamp(s.group, 0, GraphSky::TwinkleGroups - 1)]);
                ++m_drawnStars;
            }
        }
        for (const GraphSky::Meteor &m : sky->meteors()) {
            const double fade = m.fade();
            const double speed = std::hypot(m.vx, m.vy);
            if (fade <= 0 || speed <= 0)
                continue;
            const double tail = m.tailLength();
            line(m.x, m.y, m.x - m.vx / speed * tail, m.y - m.vy / speed * tail, 1.2, MeteorColour, fade * 0.9, 0);
            disc(m.x, m.y, 1.1, MeteorColour, fade);
        }
    }

    // Where every node lands, how large, and whether "Watch it grow" has reached it.
    m_projected.resize(n);
    m_radii.assign(size_t(n), 0.0);
    m_shown.assign(size_t(n), 1);
    for (int i = 0; i < n; ++i) {
        m_projected[i] = project(m_points.at(i), view.rotY, view.rotX, frame);
        if (growth && !growth->shown(i))
            m_shown[size_t(i)] = 0;
        const int size = i < m_sizes.size() ? m_sizes.at(i) : 1;
        m_radii[size_t(i)] = nodeRadius(frame.s, size, m_projected.at(i).p, flash ? flash->at(i) : 0.0);
    }

    // The tree's edges, faint and thin, under the nodes.
    for (const auto &edge : std::as_const(m_edges)) {
        const int a = edge.first;
        const int b = edge.second;
        if (a < 0 || b < 0 || a >= n || b >= n || !m_shown[size_t(a)] || !m_shown[size_t(b)])
            continue;
        const Projected &pa = m_projected.at(a);
        const Projected &pb = m_projected.at(b);
        line(pa.x, pa.y, pb.x, pb.y, EdgeWidth, EdgeColour, EdgeAlpha, EdgeAlpha);
        ++m_drawnEdges;
    }

    // The nodes: far band first, one batch per colour in each band. A flash makes a node larger
    // (in its radius) and brighter.
    const QList<NodeBatch> batches = batchNodes(m_projected, m_groups, [this](int i) { return m_shown[size_t(i)] != 0; });
    for (const NodeBatch &batch : batches) {
        const GraphRgb colour = groupColour(batch.group);
        const double alpha = bandAlpha(batch.band);
        for (int i : batch.nodes) {
            const double lit = flash ? flash->at(i) : 0.0;
            const Projected &p = m_projected.at(i);
            if (lit > 0)
                disc(p.x, p.y, m_radii[size_t(i)], mix(colour, PulseColour, std::min(1.0, lit) * 0.5),
                     std::min(1.0, alpha + lit * 0.6));
            else
                disc(p.x, p.y, m_radii[size_t(i)], colour, alpha);
            ++m_drawn;
        }
    }

    // A ring round the node the keys are on.
    if (view.selected >= 0 && view.selected < n && m_shown[size_t(view.selected)]) {
        const Projected &p = m_projected.at(view.selected);
        const double r = m_radii[size_t(view.selected)];
        ring(p.x, p.y, r + 2.5, r + 4, PulseColour, 0.7);
    }

    // The pulses: a soft glow under a bright dot, part of the way along their edge.
    if (pulses) {
        for (const GraphPulses::Pulse &pulse : pulses->pulses()) {
            const int a = pulses->from(pulse);
            const int b = pulses->to(pulse);
            if (a < 0 || b < 0 || a >= n || b >= n || !m_shown[size_t(a)] || !m_shown[size_t(b)])
                continue;
            const Projected &pa = m_projected.at(a);
            const Projected &pb = m_projected.at(b);
            const double t = std::clamp(pulse.t, 0.0, 1.0);
            const double x = pa.x + (pb.x - pa.x) * t;
            const double y = pa.y + (pb.y - pa.y) * t;
            disc(x, y, GraphPulses::GlowRadius, PulseColour, GraphPulses::GlowAlpha);
            disc(x, y, GraphPulses::DotRadius, PulseColour, GraphPulses::DotAlpha);
            ++m_drawnPulses;
        }
    }

    // The tooltip of the node under the mouse (or the keys), beside it.
    m_tooltip = {};
    if (view.tooltipNode >= 0 && view.tooltipNode < n && view.tooltipWidth > 0 && view.tooltipHeight > 0) {
        const Projected &p = m_projected.at(view.tooltipNode);
        m_tooltip = tooltipBox(p.x, p.y, view.tooltipWidth, view.tooltipHeight, view.width, view.height);
    }

    // The labels, when zoomed in far enough; while hovered, clear of the tooltip and not for its node.
    if (view.zoom >= LabelZoom) {
        QList<LabelCandidate> candidates;
        candidates.reserve(n);
        for (int i = 0; i < n; ++i) {
            const Projected &p = m_projected.at(i);
            candidates.append({p.x, p.y, p.p, i < m_sizes.size() ? m_sizes.at(i) : 1,
                               i < m_names.size() ? m_names.at(i) : QString(), m_shown[size_t(i)] != 0});
        }
        LabelsAvoid avoid;
        if (view.tooltipClearsLabels) {
            avoid.node = view.tooltipNode;
            avoid.box = m_tooltip;
        }
        m_labels = placeLabels(candidates, view.zoom, view.width, view.height, measure, avoid);
    }
}

QPointF GraphFrame::pointOf(int node) const
{
    if (node < 0 || node >= m_projected.size())
        return {-1, -1};
    return {m_projected.at(node).x, m_projected.at(node).y};
}

double GraphFrame::radiusOf(int node) const
{
    return node >= 0 && size_t(node) < m_radii.size() ? m_radii[size_t(node)] : 0.0;
}

int GraphFrame::nodeAt(double x, double y) const
{
    int found = -1;
    double nearest = std::numeric_limits<double>::max();
    for (int i = 0; i < m_projected.size(); ++i) {
        if (size_t(i) >= m_shown.size() || !m_shown[size_t(i)])
            continue;
        const GraphLook::Projected &p = m_projected.at(i);
        const double distance = std::hypot(x - p.x, y - p.y);
        if (distance <= m_radii[size_t(i)] + PickSlack && distance < nearest) {
            nearest = distance;
            found = i;
        }
    }
    return found;
}

} // namespace Riley
