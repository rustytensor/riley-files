// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include "forcelayout.h"

#include <QList>
#include <QString>

#include <functional>

namespace Riley {

class GraphTree;

// A colour as three 0..255 channels. Qt Core only, so the graph's library stays free of the GUI.
struct RILEYGRAPH_EXPORT GraphRgb
{
    int r = 0;
    int g = 0;
    int b = 0;

    // "#rrggbb"
    QString name() const;

    friend bool operator==(const GraphRgb &, const GraphRgb &) = default;
};

// How the graph looks: the numbers of servorra.com's front-page graph, as plain functions, so the
// view only draws what they say.
namespace GraphLook {

// The camera stands this far behind the [-1,1] layout.
inline constexpr double CameraDistance = 2.6;
// A point closer than this to the camera is held there, so it stays finite.
inline constexpr double NearestDepth = 0.35;
// The layout fills this share of the view's shorter side at zoom 1.
inline constexpr double ViewShare = 0.4;

inline constexpr GraphRgb Background{0x1a, 0x1b, 0x26};
inline constexpr GraphRgb Teal{0x7d, 0xd3, 0xc0};
inline constexpr GraphRgb Violet{0xbb, 0x9a, 0xf7};
inline constexpr GraphRgb Amber{0xe0, 0xaf, 0x68};
inline constexpr GraphRgb PulseColour{0xe6, 0xff, 0xfa};
inline constexpr GraphRgb LabelBright{0xc0, 0xca, 0xf5};
inline constexpr GraphRgb LabelDim{0x7f, 0x87, 0xa5};

// Tree edges: teal, faint and thin.
inline constexpr GraphRgb EdgeColour = Teal;
inline constexpr double EdgeAlpha = 0.15;
inline constexpr double EdgeWidth = 0.6;

// Group 0 is the folder walked; groups 1..25 colour the top-level folders, in turn.
inline constexpr int GroupCount = 26;
inline constexpr int VioletGroup = 15;

// Where the layout lands on the view: its middle and its scale in pixels.
struct Frame
{
    double cx = 0;
    double cy = 0;
    double s = 0;
};

// One point of the layout on the view: where it is drawn and its perspective factor (1 at the
// layout's middle depth, larger nearer the camera).
struct Projected
{
    double x = 0;
    double y = 0;
    double p = 1;
};

enum class DepthBand { Far = 0, Middle = 1, Near = 2 };

// The frame of a w x h view at a zoom: the middle of the view, s = min(w, h) * 0.4 * zoom.
Frame RILEYGRAPH_EXPORT frameFor(double width, double height, double zoom);
// p = 2.6 / max(0.35, 2.6 + z), z after the turn.
double RILEYGRAPH_EXPORT perspective(double z);
// The point turned by rotY (around the vertical axis) then rotX (around the horizontal one) and
// projected: x = cx + x*s*p, y = cy + y*s*p.
Projected RILEYGRAPH_EXPORT project(const GraphPoint &point, double rotY, double rotX, const Frame &frame);

// Far below p 0.93, middle below 1.07, near from there; drawn in that order.
DepthBand RILEYGRAPH_EXPORT bandOf(double p);
// 0.30, 0.60, 0.95.
double RILEYGRAPH_EXPORT bandAlpha(DepthBand band);

// r = (max(0.55, s/420) + size/110) * p * (1 + flash*1.6).
double RILEYGRAPH_EXPORT nodeRadius(double s, int size, double p, double flash);

// The colour of a group: a ramp in straight RGB lines from teal (group 1) to violet (group 15)
// to amber (group 25); group 0, the folder walked, is teal. Out of range is held to the ends.
GraphRgb RILEYGRAPH_EXPORT groupColour(int group);
// Each node's group: 0 for the folder walked, else 1 + (index of its top-level folder among the
// folder walked's children) % 25.
QList<int> RILEYGRAPH_EXPORT groupsOf(const GraphTree &tree);

// The nodes of one colour in one depth band, drawn as one batch.
struct NodeBatch
{
    DepthBand band = DepthBand::Far;
    int group = 0;
    QList<int> nodes;
};

// The nodes in drawing order: the far band first, then the middle, then the near one, and in each
// band one batch per group, group 0 first; empty batches are left out. `shown` (when given) leaves
// out the nodes it says no to.
QList<NodeBatch> RILEYGRAPH_EXPORT batchNodes(const QList<Projected> &points, const QList<int> &groups,
                                              const std::function<bool(int)> &shown = {});

// Labels, shown only when zoomed in.
inline constexpr double LabelZoom = 1.6;
inline constexpr int LabelMost = 36;
inline constexpr double LabelGap = 6;    // px right of the node
inline constexpr int LabelFontPx = 11;   // monospace
inline constexpr double LabelHalfHeight = 7;
inline constexpr int LabelBrightSize = 170;

// One node as the labels see it.
struct LabelCandidate
{
    double x = 0;      // on the view
    double y = 0;
    double p = 1;      // perspective factor
    int size = 0;      // 1..255
    QString name;
    bool shown = true; // false while "Watch it grow" has not reached it
};

struct Label
{
    int node = -1;
    double x = 0;      // the text's left edge
    double y = 0;      // the text's middle
    QString text;
    GraphRgb colour;
    double alpha = 0;
};

// The smallest size labelled at a zoom: max(40, 200 - (zoom - 1.6) * 90).
double RILEYGRAPH_EXPORT labelMinSize(double zoom);
// min(1, (zoom - 1.6) * 1.5) * 0.85, 0 below zoom 1.6.
double RILEYGRAPH_EXPORT labelAlpha(double zoom);
// The width of a name in 11 px monospace, as an estimate when no font is at hand.
double RILEYGRAPH_EXPORT monospaceWidth(const QString &text);
// The labels of a w x h view: none below zoom 1.6; else the nodes on the view, in front (p > 0.9),
// shown and at least labelMinSize, biggest first, at most 36, a box never overlapping one placed
// before. `width` measures a text (monospaceWidth when not given).
QList<Label> RILEYGRAPH_EXPORT placeLabels(const QList<LabelCandidate> &nodes, double zoom, double width,
                                           double height,
                                           const std::function<double(const QString &)> &measure = {});

} // namespace GraphLook

} // namespace Riley
