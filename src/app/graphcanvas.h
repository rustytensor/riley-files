// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "graphbuilder.h"
#include "graphframe.h"
#include "graphgrowth.h"
#include "graphmotion.h"
#include "graphsky.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QTextLayout;

// The graph view's drawing, as servorra.com's front-page graph: a starry sky, the tree's edges,
// the nodes coloured by top-level folder in three depth bands, pulses running along the edges,
// labels when zoomed in, and "Watch it grow". Everything it shows is worked out in riley-graph
// (GraphFrame and the classes it draws from); this item keeps the time, takes the mouse, and
// copies each frame to the scene graph as one batch of triangles and two of text.
//
// advance() moves it on by one frame; the QML around it calls it on every frame shown.
class GraphCanvas : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Riley::GraphBuilder *builder READ builder WRITE setBuilder NOTIFY builderChanged)
    // The node the keys are on, ringed; -1 for none.
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    // The node the tooltip is about (-1 for none) and the tooltip's size; tooltipRect is where it
    // goes in this item (empty for none), worked out with each frame. While the mouse is on its
    // node, the labels keep clear of it.
    Q_PROPERTY(int tooltipNode READ tooltipNode WRITE setTooltipNode NOTIFY tooltipChanged)
    Q_PROPERTY(QSizeF tooltipSize READ tooltipSize WRITE setTooltipSize NOTIFY tooltipChanged)
    Q_PROPERTY(QRectF tooltipRect READ tooltipRect NOTIFY framed)
    // "Watch it grow": whether it runs, the month it is at ("2020-03"), the nodes it has shown.
    Q_PROPERTY(bool growing READ growing NOTIFY growthChanged)
    Q_PROPERTY(QString growthCaption READ growthCaption NOTIFY growthChanged)
    Q_PROPERTY(int shownCount READ shownCount NOTIFY growthChanged)
    // Goes up by one with every frame made, for what is placed on the nodes.
    Q_PROPERTY(int revision READ revision NOTIFY framed)
    Q_PROPERTY(double zoom READ zoom NOTIFY framed)

public:
    // How long the last frame of "Watch it grow" stays before the whole graph is back.
    static constexpr double GrowthHoldMs = 1500;
    // Frame preparation times kept, for tests.
    static constexpr int TimesKept = 600;

    explicit GraphCanvas(QQuickItem *parent = nullptr);
    ~GraphCanvas() override;

    Riley::GraphBuilder *builder() const { return m_builder; }
    void setBuilder(Riley::GraphBuilder *builder);
    int selected() const { return m_selected; }
    void setSelected(int node);
    int tooltipNode() const { return m_tooltipNode; }
    void setTooltipNode(int node);
    QSizeF tooltipSize() const { return m_tooltipSize; }
    void setTooltipSize(const QSizeF &size);
    QRectF tooltipRect() const;
    bool growing() const { return m_growth.running(); }
    QString growthCaption() const { return m_growth.caption(); }
    int shownCount() const;
    int revision() const { return m_revision; }
    double zoom() const { return m_motion.zoom(); }

    // Moves time on by ms and makes the next frame.
    Q_INVOKABLE void advance(double ms);
    // Where a node is drawn in this item, (-1, -1) for none.
    Q_INVOKABLE QPointF screenPointOf(int node) const;
    // The node drawn under (x, y) of this item, or -1.
    Q_INVOKABLE int nodeAt(qreal x, qreal y) const;
    // Starts "Watch it grow" over (G or the button); seekGrowth jumps within it.
    Q_INVOKABLE void watchItGrow();
    Q_INVOKABLE void seekGrowth(double ms);
    Q_INVOKABLE void stopGrowth();

    // For tests: the frame last made, the motion, and how long each recent frame took to make (ms).
    const Riley::GraphFrame &frame() const { return m_frame; }
    Riley::GraphMotion &motion() { return m_motion; }
    QList<double> prepareTimes() const { return m_times; }
    void clearPrepareTimes() { m_times.clear(); }

signals:
    void builderChanged();
    void selectedChanged();
    void tooltipChanged();
    void growthChanged();
    void framed();
    // The mouse came over a node (-1: over none), clicked one, or double-clicked one.
    void hovered(int node);
    void clicked(int node);
    void activated(int node);

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void graphBuilt();
    void prepare();
    void setHover(int node);
    QTextLayout *layoutFor(const QString &text);
    double measure(const QString &text);

    QPointer<Riley::GraphBuilder> m_builder;
    Riley::GraphFrame m_frame;
    Riley::GraphMotion m_motion;
    Riley::GraphFlash m_flash;
    Riley::GraphPulses m_pulses;
    Riley::GraphSky m_sky;
    Riley::GrowthReplay m_growth;
    double m_growthHeld = 0;
    double m_time = 0;
    int m_selected = -1;
    int m_tooltipNode = -1;
    QSizeF m_tooltipSize;
    int m_hover = -1;
    int m_revision = 0;
    bool m_hovering = false;
    QPointF m_hoverAt;
    QPointF m_pressAt;
    QPointF m_dragAt;
    bool m_dragged = false;
    QList<double> m_times;
    bool m_frameDirty = true;

    QFont m_labelFont;
    QHash<QString, std::shared_ptr<QTextLayout>> m_layouts;
};
