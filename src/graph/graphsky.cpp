// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphsky.h"

#include <algorithm>
#include <cmath>

namespace Riley {

namespace {

double wrap(double v, double tile)
{
    const double r = std::fmod(v, tile);
    return r < 0 ? r + tile : r;
}

} // namespace

GraphSky::LayerKind GraphSky::kind(int layer)
{
    switch (layer) {
    case 0:
        return {0.12, 0.00022, 0.35, 0.8};
    case 1:
        return {0.4, 0.0001, 0.6, 1.15};
    default:
        break;
    }
    return {0.9, 0.000035, 0.9, 1.7};
}

GraphRgb GraphSky::tintColour(int tint)
{
    switch (tint) {
    case TintTeal:
        return GraphLook::Teal;
    case TintViolet:
        return GraphLook::Violet;
    case TintAmber:
        return GraphLook::Amber;
    default:
        break;
    }
    return {0xec, 0xf0, 0xff};
}

double GraphSky::Meteor::fade() const
{
    if (life <= 0)
        return 0;
    const double f = std::clamp(t / life, 0.0, 1.0);
    return f < 0.15 ? f / 0.15 : 1 - (f - 0.15) / 0.85;
}

GraphSky::GraphSky(quint32 seed)
    : m_random(seed)
{
    m_nextMeteor = std::uniform_real_distribution<double>(FirstMeteorMinMs, FirstMeteorMaxMs)(m_random);
}

void GraphSky::build(double width, double height)
{
    m_width = std::max(0.0, width);
    m_height = std::max(0.0, height);
    m_tile = std::max({1.0, m_width, m_height});
    // The same stars every time: a fixed seed of their own, apart from the shooting stars'.
    std::minstd_rand0 random(11);
    const auto unit = [&random] { return double(random() - random.min()) / double(random.max() - random.min()); };
    for (int layer = 0; layer < LayerCount; ++layer) {
        const LayerKind k = kind(layer);
        const int n = int(std::lround(k.density * m_tile * m_tile));
        QList<Star> &stars = m_layers[layer];
        stars.clear();
        stars.reserve(n);
        for (int i = 0; i < n; ++i) {
            Star s;
            s.x = unit();
            s.y = unit();
            const double u = unit();
            s.r = k.minRadius + (k.maxRadius - k.minRadius) * u * u;
            const double t = unit();
            s.tint = t < WhiteShare ? White : t < 0.86 ? TintTeal : t < 0.94 ? TintViolet : TintAmber;
            s.group = std::min(TwinkleGroups - 1, int(unit() * TwinkleGroups));
            stars.append(s);
        }
    }
}

double GraphSky::twinkle(double tMs, int group)
{
    return 0.7 + 0.3 * std::sin(tMs * 0.0011 * (1 + group * 0.37));
}

double GraphSky::layerAlpha(int layer)
{
    return 0.55 + 0.4 * kind(layer).depth;
}

QPointF GraphSky::shift(int layer, double yaw, double pitch) const
{
    const double d = kind(layer).depth;
    return {-yaw * m_width * 0.12 * d, pitch * m_height * 0.15 * d};
}

QPointF GraphSky::position(int layer, int star, double yaw, double pitch) const
{
    const Star &s = m_layers[layer].at(star);
    const QPointF by = shift(layer, yaw, pitch);
    return {wrap(s.x * m_tile + by.x(), m_tile), wrap(s.y * m_tile + by.y(), m_tile)};
}

void GraphSky::launch()
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double dir = unit(m_random) < 0.5 ? 1 : -1;
    const double angle = 0.35 + unit(m_random) * 0.35; // 20 to 40 degrees below the horizon
    const double speed = 0.9 + unit(m_random) * 0.5;
    Meteor m;
    m.x = m_width * (0.15 + unit(m_random) * 0.7);
    m.y = m_height * (0.05 + unit(m_random) * 0.45);
    m.vx = dir * std::cos(angle) * speed;
    m.vy = std::sin(angle) * speed;
    m.life = 650 + unit(m_random) * 400;
    m_meteors.append(m);
}

bool GraphSky::step(double dtMs)
{
    const double dt = std::max(0.0, dtMs);
    for (qsizetype k = m_meteors.size() - 1; k >= 0; --k) {
        Meteor &m = m_meteors[k];
        m.t += dt;
        m.x += m.vx * dt;
        m.y += m.vy * dt;
        if (m.t > m.life)
            m_meteors.removeAt(k);
    }
    m_nextMeteor -= dt;
    if (m_nextMeteor > 0)
        return false;
    m_nextMeteor = std::uniform_real_distribution<double>(MeteorMinMs, MeteorMaxMs)(m_random);
    launch();
    return true;
}

} // namespace Riley
