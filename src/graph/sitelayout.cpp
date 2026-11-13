// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "sitelayout.h"

#include "graphtree.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace Riley {

namespace {

// An octree over the points of one step, each cell holding how many points are in it and where
// their middle is.
class MassTree
{
public:
    explicit MassTree(const std::vector<GraphPoint> &points)
        : m_points(points)
    {
        m_cells.reserve(points.size() * 3 + 8);
        double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        if (!points.empty()) {
            lo[0] = hi[0] = points.front().x;
            lo[1] = hi[1] = points.front().y;
            lo[2] = hi[2] = points.front().z;
        }
        for (const GraphPoint &p : points) {
            lo[0] = std::min(lo[0], p.x), hi[0] = std::max(hi[0], p.x);
            lo[1] = std::min(lo[1], p.y), hi[1] = std::max(hi[1], p.y);
            lo[2] = std::min(lo[2], p.z), hi[2] = std::max(hi[2], p.z);
        }
        const double half = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) / 2 + 1e-6;
        m_cells.push_back(Cell{(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, half});
        for (int i = 0; i < int(points.size()); ++i)
            insert(i);
        for (Cell &cell : m_cells) {
            if (cell.mass > 0) {
                cell.mx /= cell.mass;
                cell.my /= cell.mass;
                cell.mz /= cell.mass;
            }
        }
    }

    // The sum over the other points of d / (|d|^2 + softening)^1.5, far cells taken as one.
    GraphPoint push(int i, double theta, double softening) const
    {
        const GraphPoint &p = m_points[i];
        GraphPoint sum;
        const double theta2 = theta * theta;
        int stack[512];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const Cell &cell = m_cells[stack[--top]];
            if (cell.mass == 0)
                continue;
            if (cell.child < 0 && cell.point == i && cell.mass == 1)
                continue;
            const double dx = p.x - cell.mx, dy = p.y - cell.my, dz = p.z - cell.mz;
            const double r2 = dx * dx + dy * dy + dz * dz;
            const double size = 2 * cell.half;
            if (cell.child < 0 || size * size < theta2 * r2) {
                const double soft = r2 + softening;
                const double k = cell.mass / (soft * std::sqrt(soft));
                sum.x += dx * k;
                sum.y += dy * k;
                sum.z += dz * k;
                continue;
            }
            for (int c = 0; c < 8; ++c) {
                if (m_cells[cell.child + c].mass > 0 && top < 512)
                    stack[top++] = cell.child + c;
            }
        }
        return sum;
    }

private:
    struct Cell
    {
        double ox = 0, oy = 0, oz = 0; // the cube's middle
        double half = 0;
        double mx = 0, my = 0, mz = 0; // the points' middle (a sum while building)
        double mass = 0;
        int child = -1;                // the first of eight children
        int point = -1;                // the point of a leaf
    };

    int octant(const Cell &cell, const GraphPoint &p) const
    {
        return (p.x >= cell.ox ? 1 : 0) | (p.y >= cell.oy ? 2 : 0) | (p.z >= cell.oz ? 4 : 0);
    }

    void split(int c)
    {
        const int first = int(m_cells.size());
        const Cell parent = m_cells[c];
        const double h = parent.half / 2;
        for (int k = 0; k < 8; ++k) {
            Cell child;
            child.ox = parent.ox + ((k & 1) ? h : -h);
            child.oy = parent.oy + ((k & 2) ? h : -h);
            child.oz = parent.oz + ((k & 4) ? h : -h);
            child.half = h;
            m_cells.push_back(child);
        }
        m_cells[c].child = first;
    }

    void insert(int i)
    {
        const GraphPoint &p = m_points[i];
        int c = 0;
        for (;;) {
            const double before = m_cells[c].mass;
            m_cells[c].mass += 1;
            m_cells[c].mx += p.x;
            m_cells[c].my += p.y;
            m_cells[c].mz += p.z;
            if (m_cells[c].child >= 0) {
                c = m_cells[c].child + octant(m_cells[c], p);
                continue;
            }
            if (before == 0) {
                m_cells[c].point = i;
                return;
            }
            // Points on one spot share a leaf rather than splitting without end.
            if (m_cells[c].half < 1e-9)
                return;
            const int old = m_cells[c].point;
            split(c);
            m_cells[c].point = -1;
            const GraphPoint &q = m_points[old];
            Cell &moved = m_cells[m_cells[c].child + octant(m_cells[c], q)];
            moved.mass = before;
            moved.mx = q.x * before;
            moved.my = q.y * before;
            moved.mz = q.z * before;
            moved.point = old;
            c = m_cells[c].child + octant(m_cells[c], p);
        }
    }

    const std::vector<GraphPoint> &m_points;
    std::vector<Cell> m_cells;
};

} // namespace

double SiteLayout::repulsionFor(int n)
{
    return 0.004 * std::sqrt(2411.0 / std::max(1, n));
}

double SiteLayout::stepAt(int iteration)
{
    return 0.08 * (1.0 - double(iteration) / Iterations) + 0.004;
}

QList<int> SiteLayout::sizes(const QList<qint64> &weights)
{
    QList<int> out;
    out.reserve(weights.size());
    qint64 most = 0;
    for (qint64 w : weights)
        most = std::max(most, w);
    const double scale = std::log2(double(most) + 1);
    for (qint64 w : weights) {
        const double v = scale > 0 ? std::log2(double(std::max<qint64>(0, w)) + 1) / scale * 255.0 : 0.0;
        // Cut to a whole number as the site does; the small nudge keeps 85.0 from becoming 84.
        out.append(std::clamp(int(std::floor(v + 1e-9)), 1, 255));
    }
    return out;
}

QList<int> SiteLayout::sizes(const GraphTree &tree)
{
    QList<qint64> weights;
    weights.reserve(tree.size());
    for (const GraphNode &node : tree.nodes())
        weights.append(node.filesBelow);
    return sizes(weights);
}

QList<GraphPoint> SiteLayout::start(int n)
{
    std::mt19937_64 random(Seed);
    // Box-Muller, written out so the numbers are the same with every standard library.
    const auto uniform = [&random] { return double((random() >> 11) + 1) * 0x1.0p-53; }; // (0, 1]
    std::vector<double> values;
    values.reserve(size_t(n) * 3 + 1);
    while (values.size() < size_t(n) * 3) {
        const double u = uniform(), v = uniform();
        const double r = std::sqrt(-2.0 * std::log(u));
        values.push_back(r * std::cos(2 * std::numbers::pi * v));
        values.push_back(r * std::sin(2 * std::numbers::pi * v));
    }
    QList<GraphPoint> points;
    points.reserve(n);
    for (int i = 0; i < n; ++i)
        points.append({values[3 * i], values[3 * i + 1], values[3 * i + 2]});
    return points;
}

QList<GraphPoint> SiteLayout::repulsion(const QList<GraphPoint> &points, double strength, bool exact)
{
    const int n = int(points.size());
    QList<GraphPoint> force(n);
    if (exact) {
        for (int i = 0; i < n; ++i) {
            GraphPoint sum;
            for (int j = 0; j < n; ++j) {
                if (j == i)
                    continue;
                const double dx = points[i].x - points[j].x, dy = points[i].y - points[j].y,
                             dz = points[i].z - points[j].z;
                const double soft = dx * dx + dy * dy + dz * dz + Softening;
                const double k = 1.0 / (soft * std::sqrt(soft));
                sum.x += dx * k, sum.y += dy * k, sum.z += dz * k;
            }
            force[i] = {sum.x * strength, sum.y * strength, sum.z * strength};
        }
        return force;
    }
    const std::vector<GraphPoint> copy(points.cbegin(), points.cend());
    const MassTree tree(copy);
    for (int i = 0; i < n; ++i) {
        const GraphPoint sum = tree.push(i, Theta, Softening);
        force[i] = {sum.x * strength, sum.y * strength, sum.z * strength};
    }
    return force;
}

QList<GraphPoint> SiteLayout::layOut(const QList<int> &parents, const std::atomic<bool> *stop)
{
    const int n = int(parents.size());
    if (n == 0)
        return {};
    const QList<GraphPoint> begin = start(n);
    std::vector<GraphPoint> pos(begin.cbegin(), begin.cend());
    std::vector<GraphPoint> force(n);
    const double strength = repulsionFor(n);
    for (int it = 0; it < Iterations; ++it) {
        if (stop && stop->load())
            return QList<GraphPoint>(pos.cbegin(), pos.cend());
        const double step = stepAt(it);
        {
            const MassTree tree(pos);
            for (int i = 0; i < n; ++i) {
                const GraphPoint sum = tree.push(i, Theta, Softening);
                force[i] = {sum.x * strength, sum.y * strength, sum.z * strength};
            }
        }
        for (int child = 1; child < n; ++child) {
            const int parent = parents[child];
            if (parent < 0 || parent >= n)
                continue;
            const double dx = (pos[child].x - pos[parent].x) * SpringWeight;
            const double dy = (pos[child].y - pos[parent].y) * SpringWeight;
            const double dz = (pos[child].z - pos[parent].z) * SpringWeight;
            force[parent].x += dx, force[parent].y += dy, force[parent].z += dz;
            force[child].x -= dx, force[child].y -= dy, force[child].z -= dz;
        }
        for (int i = 0; i < n; ++i) {
            force[i].x -= pos[i].x * Gravity;
            force[i].y -= pos[i].y * Gravity;
            force[i].z -= pos[i].z * Gravity;
            pos[i].x += std::clamp(force[i].x, -1.0, 1.0) * step;
            pos[i].y += std::clamp(force[i].y, -1.0, 1.0) * step;
            pos[i].z += std::clamp(force[i].z, -1.0, 1.0) * step;
        }
    }
    GraphPoint mean;
    for (const GraphPoint &p : pos)
        mean.x += p.x, mean.y += p.y, mean.z += p.z;
    mean.x /= n, mean.y /= n, mean.z /= n;
    double largest = 0;
    for (GraphPoint &p : pos) {
        p.x -= mean.x, p.y -= mean.y, p.z -= mean.z;
        largest = std::max({largest, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    }
    if (largest > 0) {
        for (GraphPoint &p : pos)
            p.x /= largest, p.y /= largest, p.z /= largest;
    }
    return QList<GraphPoint>(pos.cbegin(), pos.cend());
}

QList<GraphPoint> SiteLayout::layOut(const GraphTree &tree, const std::atomic<bool> *stop)
{
    return layOut(tree.parents(), stop);
}

} // namespace Riley
