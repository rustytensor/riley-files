// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "panewidths.h"

#include <QFontMetricsF>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

// The window width the fixed widths are drawn for; the proportional way scales from it.
constexpr double designWidth = 1280.0;
// The automatic way measures at most this many rows of a column, so a huge folder stays quick.
constexpr qsizetype mostMeasured = 5000;

const char *const draggedKeys[] = {"panes/sidebar", "panes/details", "panes/column"};

} // namespace

PaneWidths::Limits PaneWidths::limits(Pane pane)
{
    switch (pane) {
    case Sidebar:
        return {200, 140, 400};
    case Details:
        return {300, 220, 600};
    case Column:
        break;
    }
    return {260, 180, 480};
}

int PaneWidths::clampTo(Pane pane, int width)
{
    const Limits l = limits(pane);
    return std::clamp(width, l.least, l.most);
}

int PaneWidths::widthFor(Pane pane, Mode mode, int windowWidth, int natural)
{
    const Limits l = limits(pane);
    int width = l.fixed;
    switch (mode) {
    case Fixed:
        break;
    case Automatic:
        if (pane != Details && natural > 0)
            width = natural;
        break;
    case Proportional:
        if (windowWidth > 0)
            width = int(std::lround(l.fixed * double(windowWidth) / designWidth));
        break;
    }
    return clampTo(pane, width);
}

PaneWidths::Mode PaneWidths::modeFromName(const QString &name)
{
    if (name == QLatin1String("automatic"))
        return Automatic;
    if (name == QLatin1String("proportional"))
        return Proportional;
    return Fixed;
}

bool PaneWidths::paneFromName(const QString &name, Pane &pane)
{
    if (name == QLatin1String("sidebar"))
        pane = Sidebar;
    else if (name == QLatin1String("details"))
        pane = Details;
    else if (name == QLatin1String("column"))
        pane = Column;
    else
        return false;
    return true;
}

PaneWidths::PaneWidths(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    for (int p = Sidebar; p <= Column; ++p) {
        const int kept = m_settings->value(QLatin1String(draggedKeys[p]), 0).toInt();
        m_dragged[p] = kept > 0 ? clampTo(Pane(p), kept) : 0;
    }
}

PaneWidths::~PaneWidths() = default;

int PaneWidths::width(Pane pane) const
{
    if (m_dragged[pane] > 0)
        return m_dragged[pane];
    return widthFor(pane, m_mode, m_windowWidth, m_natural[pane]);
}

void PaneWidths::setWindowWidth(int width)
{
    if (width == m_windowWidth || width <= 0)
        return;
    m_windowWidth = width;
    emit widthsChanged();
}

void PaneWidths::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    for (int p = Sidebar; p <= Column; ++p) {
        m_dragged[p] = 0;
        m_settings->remove(QLatin1String(draggedKeys[p]));
    }
    m_settings->sync();
    emit widthsChanged();
}

void PaneWidths::setRowFont(const QFont &font, int sidebarRest, int columnRest)
{
    m_font = font;
    m_fontSet = true;
    m_rest[Sidebar] = sidebarRest;
    m_rest[Column] = columnRest;
    remeasure(Sidebar);
    remeasure(Column);
}

void PaneWidths::measure(Pane pane, const QStringList &names)
{
    if (pane == Details)
        return;
    m_names[pane] = names;
    remeasure(pane);
}

void PaneWidths::remeasure(Pane pane)
{
    if (!m_fontSet)
        return;
    const QFontMetricsF metrics(m_font);
    qreal widest = 0;
    const QStringList &names = m_names[pane];
    const qsizetype count = std::min(names.size(), mostMeasured);
    for (qsizetype i = 0; i < count; ++i)
        widest = std::max(widest, metrics.horizontalAdvance(names.at(i)));
    const int natural = widest > 0 ? int(std::ceil(widest)) + m_rest[pane] : 0;
    if (natural == m_natural[pane])
        return;
    m_natural[pane] = natural;
    if (m_mode == Automatic)
        emit widthsChanged();
}

void PaneWidths::beginDrag(const QString &name)
{
    Pane pane;
    if (paneFromName(name, pane))
        m_dragStart[pane] = width(pane);
}

void PaneWidths::dragBy(const QString &name, qreal dx)
{
    Pane pane;
    if (!paneFromName(name, pane) || m_dragStart[pane] <= 0)
        return;
    // The details pane is on the right: its divider moving left makes it wider.
    const int by = int(std::lround(pane == Details ? -dx : dx));
    if (by == 0 && m_dragged[pane] == 0)
        return;
    const int wanted = clampTo(pane, m_dragStart[pane] + by);
    if (wanted == m_dragged[pane])
        return;
    m_dragged[pane] = wanted;
    emit widthsChanged();
}

void PaneWidths::endDrag(const QString &name)
{
    Pane pane;
    if (!paneFromName(name, pane))
        return;
    m_dragStart[pane] = 0;
    keepDragged(pane);
}

void PaneWidths::reset(const QString &name)
{
    Pane pane;
    if (!paneFromName(name, pane))
        return;
    m_dragStart[pane] = 0;
    if (m_dragged[pane] == 0)
        return;
    m_dragged[pane] = 0;
    keepDragged(pane);
    emit widthsChanged();
}

void PaneWidths::keepDragged(Pane pane)
{
    if (m_dragged[pane] > 0)
        m_settings->setValue(QLatin1String(draggedKeys[pane]), m_dragged[pane]);
    else
        m_settings->remove(QLatin1String(draggedKeys[pane]));
    m_settings->sync();
}
