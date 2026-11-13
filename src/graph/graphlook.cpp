// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphlook.h"

#include "graphtree.h"

#include <algorithm>
#include <cmath>

namespace Riley {

QString GraphRgb::name() const
{
    return QStringLiteral("#%1%2%3")
        .arg(r, 2, 16, QLatin1Char('0'))
        .arg(g, 2, 16, QLatin1Char('0'))
        .arg(b, 2, 16, QLatin1Char('0'));
}

namespace GraphLook {

Frame frameFor(double width, double height, double zoom)
{
    return {width / 2, height / 2, std::min(width, height) * ViewShare * zoom};
}

double perspective(double z)
{
    return CameraDistance / std::max(NearestDepth, CameraDistance + z);
}

Projected project(const GraphPoint &point, double rotY, double rotX, const Frame &frame)
{
    const double cy = std::cos(rotY), sy = std::sin(rotY);
    const double cx = std::cos(rotX), sx = std::sin(rotX);
    const double x1 = point.x * cy + point.z * sy;
    const double z1 = -point.x * sy + point.z * cy;
    const double y1 = point.y * cx - z1 * sx;
    const double z2 = point.y * sx + z1 * cx;
    const double p = perspective(z2);
    return {frame.cx + x1 * frame.s * p, frame.cy + y1 * frame.s * p, p};
}

DepthBand bandOf(double p)
{
    if (p < 0.93)
        return DepthBand::Far;
    if (p < 1.07)
        return DepthBand::Middle;
    return DepthBand::Near;
}

double bandAlpha(DepthBand band)
{
    switch (band) {
    case DepthBand::Far:
        return 0.30;
    case DepthBand::Middle:
        return 0.60;
    case DepthBand::Near:
        break;
    }
    return 0.95;
}

double nodeRadius(double s, int size, double p, double flash)
{
    return (std::max(0.55, s / 420.0) + size / 110.0) * p * (1 + flash * 1.6);
}

namespace {

GraphRgb mix(const GraphRgb &from, const GraphRgb &to, double share)
{
    const auto channel = [share](int a, int b) { return int(std::lround(a + (b - a) * share)); };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

} // namespace

GraphRgb groupColour(int group)
{
    const int last = GroupCount - 1;
    group = std::clamp(group, 1, last);
    if (group <= VioletGroup)
        return mix(Teal, Violet, double(group - 1) / (VioletGroup - 1));
    return mix(Violet, Amber, double(group - VioletGroup) / (last - VioletGroup));
}

QList<int> groupsOf(const GraphTree &tree)
{
    QList<int> groups(tree.size(), 0);
    if (tree.isEmpty())
        return groups;
    // The place of each top-level item among the folder walked's children.
    QList<int> top(tree.size(), -1);
    const QList<int> &firsts = tree.node(0).children;
    for (qsizetype k = 0; k < firsts.size(); ++k)
        top[firsts.at(k)] = int(k);
    for (int i = 1; i < tree.size(); ++i) {
        const int parent = tree.node(i).parent;
        if (parent > 0 && top.at(i) < 0)
            top[i] = top.at(parent);
        groups[i] = top.at(i) < 0 ? 0 : 1 + top.at(i) % (GroupCount - 1);
    }
    return groups;
}

QList<NodeBatch> batchNodes(const QList<Projected> &points, const QList<int> &groups,
                            const std::function<bool(int)> &shown)
{
    // [band][group] -> nodes, filled in one pass.
    QList<QList<int>> buckets(3 * GroupCount);
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (shown && !shown(int(i)))
            continue;
        const int group = std::clamp(i < groups.size() ? groups.at(i) : 0, 0, GroupCount - 1);
        const int band = int(bandOf(points.at(i).p));
        buckets[band * GroupCount + group].append(int(i));
    }
    QList<NodeBatch> batches;
    for (int band = 0; band < 3; ++band) {
        for (int group = 0; group < GroupCount; ++group) {
            QList<int> &nodes = buckets[band * GroupCount + group];
            if (!nodes.isEmpty())
                batches.append({DepthBand(band), group, std::move(nodes)});
        }
    }
    return batches;
}

double labelMinSize(double zoom)
{
    return std::max(40.0, 200.0 - (zoom - LabelZoom) * 90.0);
}

double labelAlpha(double zoom)
{
    if (zoom < LabelZoom)
        return 0;
    return std::min(1.0, (zoom - LabelZoom) * 1.5) * 0.85;
}

double monospaceWidth(const QString &text)
{
    // A monospace face's advance is about 0.6 of its size.
    return text.size() * LabelFontPx * 0.6;
}

QList<Label> placeLabels(const QList<LabelCandidate> &nodes, double zoom, double width, double height,
                         const std::function<double(const QString &)> &measure)
{
    QList<Label> labels;
    if (zoom < LabelZoom)
        return labels;
    const double least = labelMinSize(zoom);
    QList<int> candidates;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        const LabelCandidate &n = nodes.at(i);
        if (n.shown && n.size >= least && n.x > 0 && n.x < width && n.y > 0 && n.y < height && n.p > 0.9)
            candidates.append(int(i));
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&nodes](int a, int b) { return nodes.at(a).size > nodes.at(b).size; });

    struct Box
    {
        double left, top, right, bottom;
    };
    QList<Box> taken;
    const double alpha = labelAlpha(zoom);
    for (int i : candidates) {
        if (labels.size() >= LabelMost)
            break;
        const LabelCandidate &n = nodes.at(i);
        const double x = n.x + LabelGap;
        const double y = n.y;
        const double w = measure ? measure(n.name) : monospaceWidth(n.name);
        const Box box{x, y - LabelHalfHeight, x + w, y + LabelHalfHeight};
        const bool overlaps = std::any_of(taken.cbegin(), taken.cend(), [&box](const Box &o) {
            return box.left < o.right && box.right > o.left && box.top < o.bottom && box.bottom > o.top;
        });
        if (overlaps)
            continue;
        taken.append(box);
        labels.append({i, x, y, n.name, n.size > LabelBrightSize ? LabelBright : LabelDim, alpha});
    }
    return labels;
}

} // namespace GraphLook

} // namespace Riley
