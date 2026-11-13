// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "forcelayout.h"

#include "graphtree.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace Riley {

namespace {

// A small random number source of our own (splitmix64), so the numbers, and with them the
// layout, are the same with every compiler and library.
class Random
{
public:
    explicit Random(quint64 seed) : m_state(seed) {}

    quint64 next()
    {
        quint64 z = (m_state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // In [0, 1).
    double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }

    // A direction, evenly spread over the sphere.
    GraphPoint direction()
    {
        const double z = 2.0 * unit() - 1.0;
        const double turn = 2.0 * std::numbers::pi * unit();
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        return {r * std::cos(turn), r * std::sin(turn), z};
    }

private:
    quint64 m_state;
};

// Far groups of nodes are pushed by as one, from their centre: an octree whose cells hold the
// number of nodes inside them and the sum of their places.
class Octree
{
public:
    void build(const std::vector<GraphPoint> &points)
    {
        m_points = &points;
        m_cells.clear();
        double low[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max()};
        double high[3] = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                          std::numeric_limits<double>::lowest()};
        for (const GraphPoint &p : points) {
            low[0] = std::min(low[0], p.x);
            low[1] = std::min(low[1], p.y);
            low[2] = std::min(low[2], p.z);
            high[0] = std::max(high[0], p.x);
            high[1] = std::max(high[1], p.y);
            high[2] = std::max(high[2], p.z);
        }
        const double half = std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2], 1e-6}) / 2.0 * 1.0001;
        Cell top;
        top.centre = {(low[0] + high[0]) / 2.0, (low[1] + high[1]) / 2.0, (low[2] + high[2]) / 2.0};
        top.half = half;
        m_cells.push_back(top);
        for (int i = 0; i < int(points.size()); ++i)
            insert(i);
    }

    // The push on node i from all the others, each pushing with strength / distance.
    GraphPoint push(int i, double strength, double theta) const
    {
        const GraphPoint &p = (*m_points)[i];
        GraphPoint total;
        const double theta2 = theta * theta;
        m_stack.clear();
        m_stack.push_back(0);
        while (!m_stack.empty()) {
            const Cell &cell = m_cells[m_stack.back()];
            m_stack.pop_back();
            if (cell.count == 0 || cell.body == i)
                continue;
            const double cx = cell.sum.x / cell.count;
            const double cy = cell.sum.y / cell.count;
            const double cz = cell.sum.z / cell.count;
            const double dx = p.x - cx;
            const double dy = p.y - cy;
            const double dz = p.z - cz;
            const double d2 = dx * dx + dy * dy + dz * dz;
            const double size = 2.0 * cell.half;
            if (cell.leaf || size * size < theta2 * d2) {
                if (d2 < 1e-18)
                    continue; // on top of each other: the spacing pass at the end parts them
                // strength * count / d along the unit vector (dx, dy, dz) / d.
                const double f = strength * cell.count / d2;
                total.x += dx * f;
                total.y += dy * f;
                total.z += dz * f;
            } else {
                for (int child : cell.children) {
                    if (child >= 0)
                        m_stack.push_back(child);
                }
            }
        }
        return total;
    }

private:
    static constexpr int MaxDepth = 32;

    struct Cell
    {
        GraphPoint centre;
        double half = 0;
        GraphPoint sum;
        int count = 0;
        int body = -1; // the one node in a leaf; -2 for several nodes at the same place
        bool leaf = true;
        int children[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    };

    int childFor(int cellIndex, const GraphPoint &p)
    {
        const Cell &cell = m_cells[cellIndex];
        const int octant = (p.x >= cell.centre.x ? 1 : 0) | (p.y >= cell.centre.y ? 2 : 0)
                           | (p.z >= cell.centre.z ? 4 : 0);
        if (cell.children[octant] >= 0)
            return cell.children[octant];
        Cell child;
        child.half = cell.half / 2.0;
        child.centre = {cell.centre.x + ((octant & 1) ? child.half : -child.half),
                        cell.centre.y + ((octant & 2) ? child.half : -child.half),
                        cell.centre.z + ((octant & 4) ? child.half : -child.half)};
        const int index = int(m_cells.size());
        m_cells[cellIndex].children[octant] = index;
        m_cells.push_back(child);
        return index;
    }

    void add(int cellIndex, const GraphPoint &p)
    {
        Cell &cell = m_cells[cellIndex];
        ++cell.count;
        cell.sum.x += p.x;
        cell.sum.y += p.y;
        cell.sum.z += p.z;
    }

    void insert(int body)
    {
        const GraphPoint &p = (*m_points)[body];
        int at = 0;
        for (int depth = 0;; ++depth) {
            add(at, p);
            if (m_cells[at].leaf) {
                if (m_cells[at].count == 1) {
                    m_cells[at].body = body;
                    return;
                }
                if (depth >= MaxDepth) {
                    m_cells[at].body = -2;
                    return;
                }
                // Split: the node already here moves down a level.
                const int old = m_cells[at].body;
                m_cells[at].body = -1;
                m_cells[at].leaf = false;
                if (old >= 0) {
                    const GraphPoint &q = (*m_points)[old];
                    const int child = childFor(at, q);
                    add(child, q);
                    m_cells[child].body = old;
                }
            }
            at = childFor(at, p);
        }
    }

    const std::vector<GraphPoint> *m_points = nullptr;
    std::vector<Cell> m_cells;
    mutable std::vector<int> m_stack;
};

// Moves nodes apart until no two are closer than `least`. Nodes are put in a grid of cells as wide
// as the spacing aimed for, so only nodes in neighbouring cells can be too close. A pass that
// moves nothing proves the spacing; when passes keep moving nodes, the whole layout is spread a
// little and the passes start again.
void keepApart(std::vector<GraphPoint> &points, double least, Random &random, const std::atomic<bool> *stop)
{
    const double aim = least * 1.05;
    const auto stopping = [stop] { return stop && stop->load(std::memory_order_relaxed); };
    const auto cellOf = [aim](double v) { return qint64(std::floor(v / aim)); };
    const auto keyOf = [](qint64 x, qint64 y, qint64 z) {
        constexpr qint64 Offset = qint64(1) << 20;
        constexpr quint64 Mask = (quint64(1) << 21) - 1;
        return (quint64(x + Offset) & Mask) | ((quint64(y + Offset) & Mask) << 21)
               | ((quint64(z + Offset) & Mask) << 42);
    };
    const int n = int(points.size());
    std::unordered_map<quint64, std::vector<int>> grid;
    for (int round = 0; round < 60; ++round) {
        for (int pass = 0; pass < 100; ++pass) {
            if (stopping())
                return;
            grid.clear();
            for (int i = 0; i < n; ++i)
                grid[keyOf(cellOf(points[i].x), cellOf(points[i].y), cellOf(points[i].z))].push_back(i);
            int moved = 0;
            for (int i = 0; i < n; ++i) {
                const qint64 cx = cellOf(points[i].x);
                const qint64 cy = cellOf(points[i].y);
                const qint64 cz = cellOf(points[i].z);
                for (qint64 dx = -1; dx <= 1; ++dx) {
                    for (qint64 dy = -1; dy <= 1; ++dy) {
                        for (qint64 dz = -1; dz <= 1; ++dz) {
                            const auto found = grid.find(keyOf(cx + dx, cy + dy, cz + dz));
                            if (found == grid.end())
                                continue;
                            for (int j : found->second) {
                                if (j <= i)
                                    continue;
                                GraphPoint &a = points[i];
                                GraphPoint &b = points[j];
                                double ux = b.x - a.x;
                                double uy = b.y - a.y;
                                double uz = b.z - a.z;
                                double d = std::sqrt(ux * ux + uy * uy + uz * uz);
                                if (d >= aim)
                                    continue;
                                if (d < 1e-12) {
                                    const GraphPoint way = random.direction();
                                    ux = way.x;
                                    uy = way.y;
                                    uz = way.z;
                                    d = 0;
                                } else {
                                    ux /= d;
                                    uy /= d;
                                    uz /= d;
                                }
                                const double step = (aim - d) / 2.0 + least * 0.01;
                                a.x -= ux * step;
                                a.y -= uy * step;
                                a.z -= uz * step;
                                b.x += ux * step;
                                b.y += uy * step;
                                b.z += uz * step;
                                ++moved;
                            }
                        }
                    }
                }
            }
            if (moved == 0)
                return;
        }
        for (GraphPoint &p : points) {
            p.x *= 1.2;
            p.y *= 1.2;
            p.z *= 1.2;
        }
    }
}

} // namespace

QList<GraphPoint> ForceLayout::layOut(const GraphTree &tree, const std::atomic<bool> *stop)
{
    return layOut(tree.parents(), stop);
}

QList<GraphPoint> ForceLayout::layOut(const QList<int> &parents, const std::atomic<bool> *stop)
{
    const int n = int(parents.size());
    if (n == 0)
        return {};
    const auto stopping = [stop] { return stop && stop->load(std::memory_order_relaxed); };
    Random random(Seed);
    const double k = EdgeLength;

    // Start: each node one edge away from its parent, in a direction from the seed.
    std::vector<GraphPoint> points(n);
    for (int i = 1; i < n; ++i) {
        const int parent = parents.at(i);
        const GraphPoint from = (parent >= 0 && parent < i) ? points[parent] : GraphPoint{};
        const GraphPoint way = random.direction();
        points[i] = {from.x + way.x * k, from.y + way.y * k, from.z + way.z * k};
    }

    // Fruchterman and Reingold's forces in 3D: k^2 / d apart between every two nodes, d^2 / k
    // together along each edge, each step no longer than a temperature that cools to nothing.
    const int rounds = n <= 2 ? 0 : (n > 1500 ? 160 : 240);
    const double hot = k * std::max(1.0, std::cbrt(double(n))) * 0.5;
    std::vector<GraphPoint> force(n);
    Octree octree;
    for (int round = 0; round < rounds; ++round) {
        if (stopping())
            break;
        octree.build(points);
        for (int i = 0; i < n; ++i)
            force[i] = octree.push(i, k * k, 0.9);
        for (int i = 1; i < n; ++i) {
            const int parent = parents.at(i);
            if (parent < 0 || parent >= n)
                continue;
            const double dx = points[parent].x - points[i].x;
            const double dy = points[parent].y - points[i].y;
            const double dz = points[parent].z - points[i].z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            // d^2 / k along the unit vector (dx, dy, dz) / d.
            const double f = d / k;
            force[i].x += dx * f;
            force[i].y += dy * f;
            force[i].z += dz * f;
            force[parent].x -= dx * f;
            force[parent].y -= dy * f;
            force[parent].z -= dz * f;
        }
        const double temperature = hot * (1.0 - double(round) / rounds) + k * 0.01;
        for (int i = 0; i < n; ++i) {
            const double length = std::sqrt(force[i].x * force[i].x + force[i].y * force[i].y
                                            + force[i].z * force[i].z);
            if (length < 1e-12)
                continue;
            const double step = std::min(length, temperature) / length;
            points[i].x += force[i].x * step;
            points[i].y += force[i].y * step;
            points[i].z += force[i].z * step;
        }
    }

    keepApart(points, MinimumDistance, random, stop);

    // The folder walked at the origin.
    const GraphPoint centre = points[0];
    for (GraphPoint &p : points) {
        p.x -= centre.x;
        p.y -= centre.y;
        p.z -= centre.z;
    }

    return QList<GraphPoint>(points.begin(), points.end());
}

double ForceLayout::closestDistance(const QList<GraphPoint> &points)
{
    if (points.size() < 2)
        return 0;
    double least = std::numeric_limits<double>::max();
    for (qsizetype i = 0; i < points.size(); ++i) {
        for (qsizetype j = i + 1; j < points.size(); ++j) {
            const double dx = points[i].x - points[j].x;
            const double dy = points[i].y - points[j].y;
            const double dz = points[i].z - points[j].z;
            least = std::min(least, dx * dx + dy * dy + dz * dz);
        }
    }
    return std::sqrt(least);
}

} // namespace Riley
