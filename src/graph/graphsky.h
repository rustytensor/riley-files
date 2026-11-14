// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileygraph_export.h"

#include "graphlook.h"

#include <QList>
#include <QPointF>
#include <QtGlobal>

#include <random>

namespace Riley {

// The starry sky behind the graph, as servorra.com's: three layers of stars (far: many and tiny,
// middle, near: few and larger) twinkling in groups, each layer shifting with the graph's turn by
// its depth (parallax), and now and then a shooting star. Times are in milliseconds.
class RILEYGRAPH_EXPORT GraphSky
{
public:
    static constexpr int LayerCount = 3;
    static constexpr int TwinkleGroups = 4;
    static constexpr double WhiteShare = 0.78;
    static constexpr double FirstMeteorMinMs = 2500;
    static constexpr double FirstMeteorMaxMs = 5500;
    static constexpr double MeteorMinMs = 7000;
    static constexpr double MeteorMaxMs = 19000;

    struct LayerKind
    {
        double depth;    // how far it moves with the turn: 0.12, 0.4, 0.9
        double density;  // stars per square pixel
        double minRadius;
        double maxRadius;
    };
    static LayerKind kind(int layer);

    enum Tint { White = 0, TintTeal = 1, TintViolet = 2, TintAmber = 3 };
    static constexpr int TintCount = 4;
    static GraphRgb tintColour(int tint);

    struct Star
    {
        double x = 0; // 0..1 of the tile
        double y = 0;
        double r = 0;
        int tint = White;
        int group = 0;
    };

    struct Meteor
    {
        double x = 0, y = 0;   // the head, in pixels
        double vx = 0, vy = 0; // pixels per ms
        double t = 0;          // ms since it started
        double life = 0;       // ms it lasts
        // 0..1: up over the first 15% of its life, then down.
        double fade() const;
        // Its tail's length in pixels.
        double tailLength() const { return 140 * fade(); }
    };

    explicit GraphSky(quint32 seed = 19);

    // Makes the stars for a w x h view: the field repeats on a square tile of the longer side,
    // each layer holding round(density * tile^2) stars, the same every time.
    void build(double width, double height);
    double width() const { return m_width; }
    double height() const { return m_height; }
    double tile() const { return m_tile; }
    const QList<Star> &stars(int layer) const { return m_layers[layer]; }

    // A twinkle group's brightness at time t in a layer, as the site's Universe.astro has it:
    // 0.7 + 0.3 * sin(t * 0.0011 * (1 + group * 0.37) + group * 1.9 + layer).
    static double twinkle(double tMs, int group, int layer);
    // A layer's alpha before twinkling: 0.55 + 0.4 * depth.
    static double layerAlpha(int layer);
    // How far a layer is shifted by the graph's turn: (-yaw * w * 0.12 * depth, pitch * h * 0.15 * depth).
    QPointF shift(int layer, double yaw, double pitch) const;
    // Where a star is drawn, the shift applied and wrapped around the tile.
    QPointF position(int layer, int star, double yaw, double pitch) const;

    // Moves the shooting stars on by dt ms and starts a new one when its time has come (the first
    // 2.5..5.5 s in, then every 7..19 s). True when one started in this step.
    bool step(double dtMs);
    const QList<Meteor> &meteors() const { return m_meteors; }
    double nextMeteorMs() const { return m_nextMeteor; }

private:
    void launch();

    double m_width = 0;
    double m_height = 0;
    double m_tile = 1;
    QList<Star> m_layers[LayerCount];
    QList<Meteor> m_meteors;
    double m_nextMeteor = 0;
    std::mt19937 m_random;
};

} // namespace Riley
