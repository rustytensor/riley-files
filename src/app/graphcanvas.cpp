// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "graphcanvas.h"

#include <QFontMetricsF>
#include <QHoverEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QTextLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cstring>

namespace {

// A press that moves less than this is a click, not a drag.
constexpr double ClickSlack = 4;

QColor colourOf(const Riley::GraphRgb &rgb, double alpha = 1)
{
    QColor c(rgb.r, rgb.g, rgb.b);
    c.setAlphaF(float(std::clamp(alpha, 0.0, 1.0)));
    return c;
}

// The scene graph's nodes of the canvas: the triangles, then the bright and the dim labels.
class CanvasNode : public QSGNode
{
public:
    QSGGeometryNode *shapes = nullptr;
    QSGTextNode *bright = nullptr;
    QSGTextNode *dim = nullptr;
};

} // namespace

GraphCanvas::GraphCanvas(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    m_labelFont.setFamily(QStringLiteral("monospace"));
    m_labelFont.setStyleHint(QFont::TypeWriter);
    m_labelFont.setPixelSize(Riley::GraphLook::LabelFontPx);
}

GraphCanvas::~GraphCanvas() = default;

void GraphCanvas::setBuilder(Riley::GraphBuilder *builder)
{
    if (builder == m_builder)
        return;
    if (m_builder)
        disconnect(m_builder, nullptr, this, nullptr);
    m_builder = builder;
    if (m_builder)
        connect(m_builder, &Riley::GraphBuilder::built, this, &GraphCanvas::graphBuilt);
    emit builderChanged();
    graphBuilt();
}

void GraphCanvas::setSelected(int node)
{
    if (node == m_selected)
        return;
    m_selected = node;
    m_frameDirty = true;
    emit selectedChanged();
}

void GraphCanvas::setTooltipNode(int node)
{
    if (node == m_tooltipNode)
        return;
    m_tooltipNode = node;
    emit tooltipChanged();
    prepare();
    update();
}

void GraphCanvas::setTooltipSize(const QSizeF &size)
{
    if (size == m_tooltipSize)
        return;
    m_tooltipSize = size;
    emit tooltipChanged();
    prepare();
    update();
}

QRectF GraphCanvas::tooltipRect() const
{
    const Riley::GraphLook::Box box = m_frame.tooltip();
    if (box.isEmpty())
        return {};
    return QRectF(QPointF(box.left, box.top), QPointF(box.right, box.bottom));
}

void GraphCanvas::graphBuilt()
{
    const bool wasGrowing = m_growth.running();
    m_growth.stop();
    QList<Riley::GraphPoint> points;
    QList<int> sizes;
    QList<int> groups;
    QStringList names;
    QList<QPair<int, int>> edges;
    if (m_builder) {
        const Riley::GraphTree &tree = m_builder->tree();
        points = m_builder->sitePositions();
        const int n = int(std::min<qsizetype>(tree.size(), points.size()));
        points.resize(n);
        sizes = m_builder->sizes();
        groups = m_builder->groups();
        names.reserve(n);
        for (int i = 0; i < n; ++i)
            names.append(tree.node(i).name);
        for (const auto &edge : tree.edges()) {
            if (edge.first < n && edge.second < n)
                edges.append(edge);
        }
    }
    m_frame.setGraph(points, sizes, groups, names, edges);
    m_flash.resize(int(points.size()));
    m_pulses.setEdges(edges);
    m_layouts.clear();
    m_hover = -1;
    m_frameDirty = true;
    if (wasGrowing)
        emit growthChanged();
    prepare();
    update();
}

int GraphCanvas::shownCount() const
{
    return m_growth.running() ? m_growth.shownCount() : m_frame.nodeCount();
}

void GraphCanvas::advance(double ms)
{
    const double dt = std::clamp(ms, 0.0, Riley::GraphMotion::LongestStepMs);
    m_time += dt;
    m_motion.step(dt);
    m_flash.fade(dt);
    m_pulses.step(dt, &m_flash);
    m_sky.step(dt);
    if (m_growth.running()) {
        const QString before = m_growth.caption();
        const int shownBefore = m_growth.shownCount();
        if (m_growth.finished()) {
            m_growthHeld += dt;
            if (m_growthHeld >= GrowthHoldMs) {
                m_growth.stop();
                emit growthChanged();
            }
        } else {
            m_growth.step(dt, &m_flash);
            if (m_growth.caption() != before || m_growth.shownCount() != shownBefore)
                emit growthChanged();
        }
    }
    prepare();
    // The node under a mouse that stays still changes as the graph turns.
    if (m_hovering)
        setHover(m_frame.nodeAt(m_hoverAt.x(), m_hoverAt.y()));
    update();
}

void GraphCanvas::prepare()
{
    QElapsedTimer clock;
    clock.start();
    Riley::GraphFrame::View view;
    view.width = width();
    view.height = height();
    view.rotY = m_motion.rotY();
    view.rotX = m_motion.rotX();
    view.zoom = m_motion.zoom();
    view.timeMs = m_time;
    view.selected = m_selected;
    view.tooltipNode = m_tooltipNode;
    view.tooltipWidth = m_tooltipSize.width();
    view.tooltipHeight = m_tooltipSize.height();
    view.tooltipClearsLabels = m_hover >= 0 && m_hover == m_tooltipNode;
    m_frame.prepare(view, &m_flash, &m_pulses, &m_sky, m_growth.running() ? &m_growth : nullptr,
                    [this](const QString &text) { return measure(text); });
    for (const Riley::GraphLook::Label &label : m_frame.labels())
        layoutFor(label.text);
    m_times.append(double(clock.nsecsElapsed()) / 1e6);
    if (m_times.size() > TimesKept)
        m_times.remove(0, m_times.size() - TimesKept);
    m_frameDirty = true;
    ++m_revision;
    emit framed();
}

double GraphCanvas::measure(const QString &text)
{
    QTextLayout *layout = layoutFor(text);
    return layout->lineCount() > 0 ? layout->lineAt(0).naturalTextWidth() : 0.0;
}

QTextLayout *GraphCanvas::layoutFor(const QString &text)
{
    auto found = m_layouts.find(text);
    if (found != m_layouts.end())
        return found->get();
    auto layout = std::make_shared<QTextLayout>(text, m_labelFont);
    layout->setCacheEnabled(true);
    layout->beginLayout();
    QTextLine line = layout->createLine();
    if (line.isValid()) {
        line.setLineWidth(1e6);
        line.setPosition(QPointF(0, 0));
    }
    layout->endLayout();
    // Names come and go with the folder shown; the cache never grows past a few screens of them.
    if (m_layouts.size() > 4000)
        m_layouts.clear();
    return m_layouts.insert(text, layout)->get();
}

QPointF GraphCanvas::screenPointOf(int node) const
{
    return m_frame.pointOf(node);
}

int GraphCanvas::nodeAt(qreal x, qreal y) const
{
    return m_frame.nodeAt(x, y);
}

void GraphCanvas::watchItGrow()
{
    if (!m_builder || m_frame.nodeCount() == 0)
        return;
    m_motion.touch();
    m_growthHeld = 0;
    m_growth.start(m_builder->bornTimes());
    m_growth.seek(0, &m_flash);
    emit growthChanged();
    prepare();
    update();
}

void GraphCanvas::seekGrowth(double ms)
{
    if (!m_growth.running())
        return;
    m_growthHeld = 0;
    m_growth.seek(ms, &m_flash);
    emit growthChanged();
    prepare();
    update();
}

void GraphCanvas::stopGrowth()
{
    if (!m_growth.running())
        return;
    m_growth.stop();
    emit growthChanged();
    prepare();
    update();
}

void GraphCanvas::setHover(int node)
{
    if (node == m_hover)
        return;
    m_hover = node;
    emit hovered(node);
}

void GraphCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        m_sky.build(newGeometry.width(), newGeometry.height());
        prepare();
        update();
    }
}

void GraphCanvas::mousePressEvent(QMouseEvent *event)
{
    m_motion.press();
    m_pressAt = m_dragAt = event->position();
    m_dragged = false;
    event->accept();
}

void GraphCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF at = event->position();
    if (!m_dragged && QLineF(at, m_pressAt).length() < ClickSlack)
        return;
    m_dragged = true;
    m_motion.dragBy(at.x() - m_dragAt.x(), at.y() - m_dragAt.y());
    m_dragAt = at;
    event->accept();
}

void GraphCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    m_motion.release();
    if (!m_dragged)
        emit clicked(m_frame.nodeAt(event->position().x(), event->position().y()));
    event->accept();
}

void GraphCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    m_motion.touch();
    emit activated(m_frame.nodeAt(event->position().x(), event->position().y()));
    event->accept();
}

void GraphCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_motion.touch();
    m_hovering = true;
    m_hoverAt = event->position();
    setHover(m_frame.nodeAt(m_hoverAt.x(), m_hoverAt.y()));
}

void GraphCanvas::hoverLeaveEvent(QHoverEvent *)
{
    m_hovering = false;
    setHover(-1);
}

void GraphCanvas::wheelEvent(QWheelEvent *event)
{
    // A browser's deltaY is the other way round from Qt's angle: up (away) is negative there.
    m_motion.wheel(-event->angleDelta().y());
    event->accept();
}

QSGNode *GraphCanvas::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *root = static_cast<CanvasNode *>(old);
    if (!root) {
        root = new CanvasNode;
        root->shapes = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0,
                                         QSGGeometry::UnsignedIntType);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        root->shapes->setGeometry(geometry);
        root->shapes->setFlag(QSGNode::OwnsGeometry);
        root->shapes->setMaterial(new QSGVertexColorMaterial);
        root->shapes->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(root->shapes);
        root->bright = window()->createTextNode();
        root->dim = window()->createTextNode();
        root->appendChildNode(root->bright);
        root->appendChildNode(root->dim);
        m_frameDirty = true;
    }
    if (!m_frameDirty)
        return root;
    m_frameDirty = false;

    static_assert(sizeof(Riley::GraphVertex) == sizeof(QSGGeometry::ColoredPoint2D));
    QSGGeometry *geometry = root->shapes->geometry();
    const auto &vertices = m_frame.vertices();
    const auto &indices = m_frame.indices();
    geometry->allocate(int(vertices.size()), int(indices.size()));
    if (!vertices.empty())
        std::memcpy(geometry->vertexData(), vertices.data(), vertices.size() * sizeof(Riley::GraphVertex));
    if (!indices.empty())
        std::memcpy(geometry->indexData(), indices.data(), indices.size() * sizeof(quint32));
    root->shapes->markDirty(QSGNode::DirtyGeometry);

    root->bright->clear();
    root->dim->clear();
    const QList<Riley::GraphLook::Label> &labels = m_frame.labels();
    const double alpha = labels.isEmpty() ? 0.0 : labels.first().alpha;
    root->bright->setColor(colourOf(Riley::GraphLook::LabelBright, alpha));
    root->dim->setColor(colourOf(Riley::GraphLook::LabelDim, alpha));
    const QFontMetricsF metrics(m_labelFont);
    for (const Riley::GraphLook::Label &label : labels) {
        auto found = m_layouts.constFind(label.text);
        if (found == m_layouts.cend())
            continue;
        QSGTextNode *node = label.colour == Riley::GraphLook::LabelBright ? root->bright : root->dim;
        node->addTextLayout(QPointF(label.x, label.y - metrics.height() / 2), found->get());
    }
    return root;
}
