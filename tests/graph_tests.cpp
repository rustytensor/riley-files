// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The graph view's library on real files: the tree walked into nodes and edges, folders only for
// a large tree, and a layout that is the same every time, keeps nodes apart and is made off the
// UI thread. Then the look of servorra.com's graph as numbers (v0.8): projection, colours, the
// site's layout, node sizes, motion, pulses, the sky, labels and "Watch it grow". Then v0.9: the
// palette and the twinkle exactly as the site's code has them, and labels kept clear of the tooltip.

#include "forcelayout.h"
#include "graphbuilder.h"
#include "graphframe.h"
#include "graphgrowth.h"
#include "graphlook.h"
#include "graphmotion.h"
#include "graphsky.h"
#include "graphtree.h"
#include "sitelayout.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numbers>

#include <unistd.h>

using namespace Riley;
using namespace Qt::StringLiterals;

class GraphTests : public QObject
{
    Q_OBJECT

private slots:
    void graphBuildsFromFolders();
    void graphCapsLargeTrees();
    void graphLayoutIsStable();
    void graphWalkSkipsLinkLoops();
    void graphProjectsLikeTheSite();
    void graphPaletteExactlyLikeTheSite();
    void graphLayoutMatchesTheSite();
    void graphNodesDrawLikeTheSite();
    void graphMovesLikeTheSite();
    void graphPulsesGlow();
    void graphSkyLikeTheSite();
    void graphLabelsLikeTheSite();
    void graphGrowsOverTime();
    void graphTwinkleExactlyLikeTheSite();
    void graphLabelNotUnderTooltip();
};

namespace {

bool makeFile(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly);
}

// root/a/a1, root/a/a2, root/b/b1 (folders), root/f1.txt, root/f2.txt, root/a/a1/x.txt.
bool makeSmallTree(const QString &root)
{
    return QDir().mkpath(root + QStringLiteral("/a/a2")) && QDir().mkpath(root + QStringLiteral("/b/b1"))
           && makeFile(root + QStringLiteral("/f1.txt")) && makeFile(root + QStringLiteral("/f2.txt"))
           && makeFile(root + QStringLiteral("/a/a1/x.txt"));
}

// Builds the graph of root and waits for it; the time it took in ms, or -1.
qint64 build(GraphBuilder &builder, const QString &root, int timeoutMs)
{
    QSignalSpy built(&builder, &GraphBuilder::built);
    QElapsedTimer clock;
    clock.start();
    builder.start(root, false);
    if (!built.wait(timeoutMs))
        return -1;
    return clock.elapsed();
}

QString nameOf(const GraphTree &tree, int index)
{
    return index < 0 ? QStringLiteral("(none)") : tree.node(index).name;
}

} // namespace

void GraphTests::graphBuildsFromFolders()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(QStringLiteral("root"));
    QVERIFY(makeSmallTree(root));

    GraphBuilder builder;
    QVERIFY(build(builder, root, 5000) >= 0);
    const GraphTree &tree = builder.tree();

    QStringList names;
    for (const GraphNode &node : tree.nodes())
        names.append(node.name);
    names.sort();
    QCOMPARE(names, (QStringList{u"a"_s, u"a1"_s, u"a2"_s, u"b"_s, u"b1"_s, u"f1.txt"_s, u"f2.txt"_s,
                                 u"root"_s, u"x.txt"_s}));
    QCOMPARE(tree.node(0).name, u"root"_s);
    QCOMPARE(tree.node(0).parent, -1);

    const auto edges = tree.edges();
    QCOMPARE(edges.size(), 8);
    for (const auto &edge : edges) {
        const GraphNode &child = tree.node(edge.first);
        const GraphNode &parent = tree.node(edge.second);
        QVERIFY2(parent.isDir, qPrintable(child.name));
        QCOMPARE(child.path, parent.path + QLatin1Char('/') + child.name);
    }
    QCOMPARE(nameOf(tree, tree.node(tree.indexOf(root + u"/a/a1"_s)).parent), u"a"_s);
    QCOMPARE(nameOf(tree, tree.node(tree.indexOf(root + u"/a/a1/x.txt"_s)).parent), u"a1"_s);
    QCOMPARE(nameOf(tree, tree.node(tree.indexOf(root + u"/f1.txt"_s)).parent), u"root"_s);
    QCOMPARE(nameOf(tree, tree.node(tree.indexOf(root + u"/b/b1"_s)).parent), u"b"_s);
    QCOMPARE(tree.node(0).filesBelow, qint64(3));
    QVERIFY(!tree.foldersOnly());
    QVERIFY(builder.note().isEmpty());

    // Moving through it the way the view does: root, its first child a, then b, back to root.
    QCOMPARE(nameOf(tree, builder.firstChildOf(0)), u"a"_s);
    QCOMPARE(nameOf(tree, builder.nextSiblingOf(builder.firstChildOf(0))), u"b"_s);
    QCOMPARE(builder.parentOf(builder.firstChildOf(0)), 0);
    QCOMPARE(builder.label(tree.indexOf(root + u"/a"_s)), u"a · 1 file"_s);

    QVERIFY(!builder.threads().isEmpty());
    QVERIFY2(!builder.threads().contains(QThread::currentThreadId()), "the tree was walked on the UI thread");
}

void GraphTests::graphCapsLargeTrees()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(QStringLiteral("big"));
    for (int folder = 0; folder < 200; ++folder) {
        const QString dir = root + QStringLiteral("/d%1").arg(folder, 3, 10, QLatin1Char('0'));
        QVERIFY(QDir().mkpath(dir));
        for (int file = 0; file < 100; ++file)
            QVERIFY(makeFile(dir + QStringLiteral("/f%1.txt").arg(file)));
    }

    GraphBuilder builder;
    const qint64 took = build(builder, root, 10000);
    QVERIFY2(took >= 0, "no graph within 10 s");
    QVERIFY2(took < 5000, qPrintable(QStringLiteral("took %1 ms").arg(took)));
    QCOMPARE(builder.count(), 201);
    QVERIFY(builder.foldersOnly());
    QCOMPARE(builder.tree().fileCount(), qint64(20000));
    QCOMPARE(builder.note(), u"Showing folders only (20,000 files)."_s);
    for (const GraphNode &node : builder.tree().nodes())
        QVERIFY2(node.isDir, qPrintable(node.path));
    QCOMPARE(builder.tree().node(0).filesBelow, qint64(20000));
    QCOMPARE(builder.positions().size(), 201);

    QVERIFY(!builder.threads().isEmpty());
    QVERIFY2(!builder.threads().contains(QThread::currentThreadId()), "the tree was walked on the UI thread");
}

void GraphTests::graphLayoutIsStable()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());

    // The same small tree, twice: the same places.
    const QString small = scratch.filePath(QStringLiteral("root"));
    QVERIFY(makeSmallTree(small));
    GraphBuilder first;
    GraphBuilder second;
    QVERIFY(build(first, small, 5000) >= 0);
    QVERIFY(build(second, small, 5000) >= 0);
    QCOMPARE(first.positions().size(), 9);
    QVERIFY(first.positions() == second.positions());
    QVERIFY(ForceLayout::closestDistance(first.positions()) >= ForceLayout::MinimumDistance);

    // 2,600 nodes: the folder, 100 folders and 2,499 files.
    const QString large = scratch.filePath(QStringLiteral("large"));
    int files = 0;
    for (int folder = 0; folder < 100; ++folder) {
        const QString dir = large + QStringLiteral("/d%1").arg(folder, 3, 10, QLatin1Char('0'));
        QVERIFY(QDir().mkpath(dir));
        for (int file = 0; file < 25 && files < 2499; ++file, ++files)
            QVERIFY(makeFile(dir + QStringLiteral("/f%1.txt").arg(file)));
    }
    GraphBuilder big;
    const qint64 took = build(big, large, 10000);
    QVERIFY2(took >= 0, "no layout within 10 s");
    QCOMPARE(big.count(), 2600);
    QVERIFY(!big.foldersOnly());
    QVERIFY2(took < 3000, qPrintable(QStringLiteral("took %1 ms").arg(took)));
    QCOMPARE(big.positions().size(), 2600);
    const double closest = ForceLayout::closestDistance(big.positions());
    QVERIFY2(closest >= ForceLayout::MinimumDistance, qPrintable(QStringLiteral("closest %1").arg(closest)));
    QVERIFY2(!big.threads().contains(QThread::currentThreadId()), "the layout ran on the UI thread");

    GraphBuilder again;
    QVERIFY(build(again, large, 10000) >= 0);
    QVERIFY(big.positions() == again.positions());
    // Not all on one spot or one line: the layout spreads in all three directions.
    double spread[3] = {0, 0, 0};
    for (const GraphPoint &p : big.positions()) {
        spread[0] = std::max(spread[0], std::abs(p.x));
        spread[1] = std::max(spread[1], std::abs(p.y));
        spread[2] = std::max(spread[2], std::abs(p.z));
    }
    QVERIFY(spread[0] > ForceLayout::EdgeLength && spread[1] > ForceLayout::EdgeLength
            && spread[2] > ForceLayout::EdgeLength);
}

// The small tree plus root/a/loop -> .. (a link to the folder above a): the walk ends, the link is
// one more node, a file with nothing below it, and no path goes through it.
void GraphTests::graphWalkSkipsLinkLoops()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(QStringLiteral("root"));
    QVERIFY(makeSmallTree(root));
    QVERIFY(::symlink("..", QFile::encodeName(root + u"/a/loop"_s).constData()) == 0);
    QVERIFY(QFileInfo(root + u"/a/loop"_s).isDir());

    GraphBuilder builder;
    QVERIFY2(build(builder, root, 5000) >= 0, "the walk did not end within 5 s");
    const GraphTree &tree = builder.tree();

    QStringList names;
    for (const GraphNode &node : tree.nodes()) {
        names.append(node.name);
        QVERIFY2(!node.path.contains(u"loop/"_s), qPrintable(node.path));
    }
    names.sort();
    QCOMPARE(names, (QStringList{u"a"_s, u"a1"_s, u"a2"_s, u"b"_s, u"b1"_s, u"f1.txt"_s, u"f2.txt"_s,
                                 u"loop"_s, u"root"_s, u"x.txt"_s}));
    QCOMPARE(tree.size(), 10);
    QCOMPARE(tree.edges().size(), 9);

    const int loop = tree.indexOf(root + u"/a/loop"_s);
    QVERIFY(loop > 0);
    QVERIFY(!tree.node(loop).isDir);
    QVERIFY(tree.node(loop).children.isEmpty());
    QCOMPARE(tree.node(loop).filesBelow, qint64(0));
    QCOMPARE(nameOf(tree, tree.node(loop).parent), u"a"_s);
}

namespace {

bool near(double a, double b, double within)
{
    return std::abs(a - b) <= within;
}

QString describe(double value)
{
    return QString::number(value, 'f', 6);
}

// Runs git in a folder with fixed dates and no user configuration; true when it exits 0.
bool git(const QString &folder, const QStringList &arguments, qint64 when = 0)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"GIT_CONFIG_NOSYSTEM"_s, u"1"_s);
    env.insert(u"GIT_CONFIG_GLOBAL"_s, u"/dev/null"_s);
    if (when > 0) {
        const QString date = u"@%1 +0000"_s.arg(when);
        env.insert(u"GIT_AUTHOR_DATE"_s, date);
        env.insert(u"GIT_COMMITTER_DATE"_s, date);
    }
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(folder);
    QStringList all{u"-c"_s, u"user.name=Riley Test"_s, u"-c"_s, u"user.email=test@example.invalid"_s,
                    u"-c"_s, u"init.defaultBranch=main"_s, u"-c"_s, u"commit.gpgsign=false"_s};
    all += arguments;
    process.start(u"git"_s, all);
    return process.waitForFinished(20000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// The parents of a tree of 2,600 nodes: the folder, 100 folders below it and 2,499 files in them.
QList<int> bigParents()
{
    QList<int> parents{-1};
    for (int folder = 0; folder < 100; ++folder)
        parents.append(0);
    for (int file = 0; file < 2499; ++file)
        parents.append(1 + file % 100);
    return parents;
}

} // namespace

void GraphTests::graphProjectsLikeTheSite()
{
    using namespace GraphLook;
    const Frame frame = frameFor(1000, 1000, 1);
    QCOMPARE(frame.cx, 500.0);
    QCOMPARE(frame.cy, 500.0);
    QCOMPARE(frame.s, 400.0);
    QCOMPARE(frameFor(1000, 600, 2).s, 480.0);

    Projected at = project({0, 0, 0}, 0, 0, frame);
    QCOMPARE(at.x, 500.0);
    QCOMPARE(at.y, 500.0);
    QCOMPARE(at.p, 1.0);
    at = project({1, 0, 0}, 0, 0, frame);
    QCOMPARE(at.x, 900.0);
    QCOMPARE(at.y, 500.0);

    at = project({0, 0, 1}, 0, 0, frame);
    QVERIFY2(near(at.p, 0.72222, 0.00001), qPrintable(describe(at.p)));
    QCOMPARE(bandOf(at.p), DepthBand::Far);
    QCOMPARE(bandAlpha(bandOf(at.p)), 0.30);
    at = project({0, 0, -0.5}, 0, 0, frame);
    QVERIFY2(near(at.p, 1.23810, 0.00001), qPrintable(describe(at.p)));
    QCOMPARE(bandOf(at.p), DepthBand::Near);
    QCOMPARE(bandAlpha(bandOf(at.p)), 0.95);
    at = project({0, 0, 0.1}, 0, 0, frame);
    QVERIFY2(near(at.p, 0.96296, 0.00001), qPrintable(describe(at.p)));
    QCOMPARE(bandOf(at.p), DepthBand::Middle);
    QCOMPARE(bandAlpha(bandOf(at.p)), 0.60);

    // Past the camera the factor is held: 2.6 / 0.35.
    QVERIFY(near(perspective(-5), 2.6 / 0.35, 1e-9));
    // A quarter turn around the vertical axis takes x to depth: (1,0,0) turned by pi/2 is (0,0,-1),
    // nearer the camera, drawn at the middle.
    at = project({1, 0, 0}, std::numbers::pi / 2, 0, frame);
    QVERIFY2(near(at.x, 500, 1e-9) && near(at.p, 2.6 / 1.6, 1e-9), qPrintable(describe(at.x) + u' ' + describe(at.p)));
}

void GraphTests::graphPaletteExactlyLikeTheSite()
{
    using namespace GraphLook;
    // The site's readColors: colour(i) is the ramp at t = i / 25, teal..violet over t < 0.6, then
    // violet..amber, each channel rounded.
    QCOMPARE(GroupCount, 26);
    QCOMPARE(groupColour(0).name(), u"#7dd3c0"_s);
    QCOMPARE(groupColour(1).name(), u"#81cfc4"_s);
    QCOMPARE(groupColour(15).name(), u"#bb9af7"_s);
    QCOMPARE(groupColour(25).name(), u"#e0af68"_s);
    QCOMPARE(Background.name(), u"#1a1b26"_s);
    QCOMPARE(EdgeColour.name(), u"#7dd3c0"_s);
    QCOMPARE(EdgeAlpha, 0.15);
    QCOMPARE(EdgeWidth, 0.6);

    // The small tree: root's children are a, b, f1.txt, f2.txt (folders first), groups 1..4; what
    // is below a top-level folder takes its group.
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(u"root"_s);
    QVERIFY(makeSmallTree(root));
    const GraphTree tree = GraphTree::walk(root, false);
    const QList<int> groups = groupsOf(tree);
    const auto groupOf = [&](const QString &relative) { return groups.at(tree.indexOf(root + relative)); };
    QCOMPARE(groups.at(0), 0);
    QCOMPARE(groupOf(u"/a"_s), 1);
    QCOMPARE(groupOf(u"/b"_s), 2);
    QCOMPARE(groupOf(u"/f1.txt"_s), 3);
    QCOMPARE(groupOf(u"/f2.txt"_s), 4);
    QCOMPARE(groupOf(u"/a/a1/x.txt"_s), 1);
    QCOMPARE(groupOf(u"/b/b1"_s), 2);

    // 27 top-level folders: the 26th and 27th wrap round to groups 1 and 2.
    const QString wide = scratch.filePath(u"wide"_s);
    for (int k = 0; k < 27; ++k)
        QVERIFY(QDir().mkpath(wide + u"/t%1"_s.arg(k, 2, 10, QLatin1Char('0'))));
    const GraphTree many = GraphTree::walk(wide, false);
    const QList<int> wrapped = groupsOf(many);
    QCOMPARE(wrapped.at(many.indexOf(wide + u"/t00"_s)), 1);
    QCOMPARE(wrapped.at(many.indexOf(wide + u"/t24"_s)), 25);
    QCOMPARE(wrapped.at(many.indexOf(wide + u"/t25"_s)), 1);
    QCOMPARE(wrapped.at(many.indexOf(wide + u"/t26"_s)), 2);
}

void GraphTests::graphLayoutMatchesTheSite()
{
    QCOMPARE(SiteLayout::sizes(QList<qint64>{0, 1, 3, 7}), (QList<int>{1, 85, 170, 255}));
    QVERIFY(near(SiteLayout::repulsionFor(2411), 0.004, 1e-15));
    QVERIFY(near(SiteLayout::repulsionFor(2600), 0.004 * std::sqrt(2411.0 / 2600), 1e-15));
    QVERIFY(near(SiteLayout::stepAt(0), 0.084, 1e-15));
    QVERIFY(near(SiteLayout::stepAt(130), 0.044, 1e-15));
    QVERIFY(near(SiteLayout::stepAt(259), 0.08 / 260 + 0.004, 1e-15));
    QCOMPARE(SiteLayout::Iterations, 260);
    QCOMPARE(SiteLayout::Seed, quint64(7));

    // The start is a standard normal distribution: 30,000 values, mean 0 and variance 1.
    const QList<GraphPoint> start = SiteLayout::start(10000);
    double sum = 0, squares = 0;
    for (const GraphPoint &p : start) {
        sum += p.x + p.y + p.z;
        squares += p.x * p.x + p.y * p.y + p.z * p.z;
    }
    const double mean = sum / 30000, variance = squares / 30000 - mean * mean;
    QVERIFY2(std::abs(mean) < 0.03 && std::abs(variance - 1) < 0.04, qPrintable(describe(mean) + u' ' + describe(variance)));
    QVERIFY(SiteLayout::start(10000) == start);

    // The octree's push is close to the exact sum over every pair (RMS error under 3%).
    const QList<GraphPoint> points = SiteLayout::start(2600);
    const QList<GraphPoint> exact = SiteLayout::repulsion(points, 1, true);
    const QList<GraphPoint> fast = SiteLayout::repulsion(points, 1, false);
    double error = 0, size = 0;
    for (int i = 0; i < 2600; ++i) {
        const double dx = fast[i].x - exact[i].x, dy = fast[i].y - exact[i].y, dz = fast[i].z - exact[i].z;
        error += dx * dx + dy * dy + dz * dz;
        size += exact[i].x * exact[i].x + exact[i].y * exact[i].y + exact[i].z * exact[i].z;
    }
    QVERIFY2(std::sqrt(error / size) < 0.03, qPrintable(describe(std::sqrt(error / size))));

    // 2,600 nodes: the same every time, centred, the largest coordinate exactly 1.
    const QList<int> parents = bigParents();
    QElapsedTimer clock;
    clock.start();
    const QList<GraphPoint> laid = SiteLayout::layOut(parents);
    const qint64 took = clock.elapsed();
    QCOMPARE(laid.size(), 2600);
    QVERIFY2(took < 5000, qPrintable(u"took %1 ms"_s.arg(took)));
    QVERIFY(SiteLayout::layOut(parents) == laid);
    double largest = 0, mx = 0, my = 0, mz = 0;
    for (const GraphPoint &p : laid) {
        largest = std::max({largest, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
        mx += p.x, my += p.y, mz += p.z;
    }
    QCOMPARE(largest, 1.0);
    QVERIFY2(std::abs(mx / 2600) < 1e-9 && std::abs(my / 2600) < 1e-9 && std::abs(mz / 2600) < 1e-9,
             qPrintable(describe(mx / 2600)));
    // The springs hold: a file sits nearer its folder than nodes sit to one another on average.
    double edge = 0, apart = 0;
    for (int i = 1; i < 2600; ++i) {
        const GraphPoint &a = laid[i], &b = laid[parents[i]], &c = laid[(i * 7919) % 2600];
        edge += std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
        apart += std::hypot(a.x - c.x, a.y - c.y, a.z - c.z);
    }
    QVERIFY2(edge < apart * 0.5, qPrintable(describe(edge / 2599) + u" vs "_s + describe(apart / 2599)));

    // On real files: sizes from the files below each folder.
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(u"root"_s);
    QVERIFY(makeSmallTree(root));
    const GraphTree tree = GraphTree::walk(root, false);
    const QList<int> sizes = SiteLayout::sizes(tree);
    // root holds 3 files (the largest), a holds 1, b none: log2(2)/log2(4)*255 = 127.
    QCOMPARE(sizes.at(0), 255);
    QCOMPARE(sizes.at(tree.indexOf(root + u"/a"_s)), 127);
    QCOMPARE(sizes.at(tree.indexOf(root + u"/b"_s)), 1);
    QCOMPARE(sizes.at(tree.indexOf(root + u"/f1.txt"_s)), 1);
    const QList<GraphPoint> small = SiteLayout::layOut(tree);
    QCOMPARE(small.size(), tree.size());
    QVERIFY(SiteLayout::layOut(tree) == small);
}

void GraphTests::graphNodesDrawLikeTheSite()
{
    using namespace GraphLook;
    const double r = nodeRadius(400, 255, 1, 0);
    QVERIFY2(near(r, 3.2706, 0.0001), qPrintable(describe(r)));
    QVERIFY(near(nodeRadius(400, 255, 1, 1), r * 2.6, 1e-12));
    QVERIFY(near(nodeRadius(400, 255, 0.5, 0), r * 0.5, 1e-12));
    // A small view: the base radius is held at 0.55.
    QVERIFY(near(nodeRadius(100, 0, 1, 0), 0.55, 1e-12));

    // Five nodes: far, near, middle, far, near; groups 2, 1, 1, 1, 1. Drawn far first (group 1
    // then 2), then middle, then near.
    const QList<Projected> points{{0, 0, 0.8}, {0, 0, 1.2}, {0, 0, 1.0}, {0, 0, 0.92}, {0, 0, 1.07}};
    const QList<int> groups{2, 1, 1, 1, 1};
    const QList<NodeBatch> batches = batchNodes(points, groups);
    QCOMPARE(batches.size(), 4);
    QCOMPARE(batches[0].band, DepthBand::Far);
    QCOMPARE(batches[0].group, 1);
    QCOMPARE(batches[0].nodes, QList<int>{3});
    QCOMPARE(batches[1].band, DepthBand::Far);
    QCOMPARE(batches[1].group, 2);
    QCOMPARE(batches[1].nodes, QList<int>{0});
    QCOMPARE(batches[2].band, DepthBand::Middle);
    QCOMPARE(batches[2].nodes, QList<int>{2});
    QCOMPARE(batches[3].band, DepthBand::Near);
    QCOMPARE(batches[3].nodes, (QList<int>{1, 4}));
    // Nodes not shown are left out.
    const QList<NodeBatch> some = batchNodes(points, groups, [](int i) { return i != 3; });
    QCOMPARE(some.size(), 3);
    QCOMPARE(some[0].nodes, QList<int>{0});
}

void GraphTests::graphMovesLikeTheSite()
{
    GraphMotion motion;
    QCOMPARE(motion.rotY(), 0.6);
    QCOMPARE(motion.rotX(), -0.25);
    QCOMPARE(motion.zoom(), 1.0);

    // 10 s with no input in 16 ms frames: still for 4 s, then 0.00012 rad per ms for 6 s.
    for (int frame = 0; frame < 625; ++frame)
        motion.step(16);
    QVERIFY2(near(motion.rotY() - 0.6, 0.72, 0.02), qPrintable(describe(motion.rotY() - 0.6)));
    QCOMPARE(motion.rotX(), -0.25);

    // A drag of 100 px to the right turns by 0.5 at once; after it the turn runs on and dies out.
    GraphMotion drag;
    drag.press();
    drag.dragBy(100, 0);
    QVERIFY(near(drag.rotY(), 1.1, 1e-12));
    drag.dragBy(4, 0);
    const double released = drag.rotY();
    drag.release();
    drag.step(16);
    QVERIFY2(drag.rotY() > released, "no inertia after the drag");
    for (int frame = 0; frame < 120; ++frame)
        drag.step(16);
    const double settled = drag.rotY();
    drag.step(16);
    QVERIFY2(near(drag.rotY(), settled, 1e-4), qPrintable(describe(drag.rotY() - settled)));
    // The tilt is held within +-1.4.
    drag.press();
    drag.dragBy(0, 1000);
    QCOMPARE(drag.rotX(), 1.4);
    drag.dragBy(0, -5000);
    QCOMPARE(drag.rotX(), -1.4);
    drag.release();
    for (int frame = 0; frame < 30; ++frame)
        drag.step(16);
    QVERIFY(drag.rotX() >= -1.4);

    // The wheel: one notch in multiplies the target by exp(0.18); the zoom eases there.
    GraphMotion wheel;
    wheel.wheel(-120);
    QVERIFY(near(wheel.zoomTarget(), std::exp(0.18), 1e-12));
    QCOMPARE(wheel.zoom(), 1.0);
    wheel.step(16);
    const double eased = 1 + (std::exp(0.18) - 1) * (1 - std::exp(-16.0 / 140));
    QVERIFY2(near(wheel.zoom(), eased, 1e-12), qPrintable(describe(wheel.zoom())));
    for (int k = 0; k < 40; ++k)
        wheel.wheel(-120);
    QCOMPARE(wheel.zoomTarget(), 7.0);
    for (int k = 0; k < 80; ++k)
        wheel.wheel(120);
    QCOMPARE(wheel.zoomTarget(), 0.5);
    // Input keeps it from spinning: a wheel step resets the 4 s.
    GraphMotion idle;
    for (int frame = 0; frame < 300; ++frame) {
        idle.step(16);
        if (frame % 100 == 0)
            idle.wheel(0);
    }
    QCOMPARE(idle.rotY(), 0.6);
}

void GraphTests::graphPulsesGlow()
{
    QCOMPARE(GraphPulses::GlowRadius, 4.5);
    QCOMPARE(GraphPulses::GlowAlpha, 0.18);
    QCOMPARE(GraphPulses::DotRadius, 1.6);
    QCOMPARE(GraphPulses::DotAlpha, 0.95);
    QCOMPARE(GraphLook::PulseColour.name(), u"#e6fffa"_s);

    // One edge: one pulse; it arrives after ceil(1 / (v * 16 * 0.06)) frames and flashes its end.
    GraphPulses one;
    one.setEdges({{0, 1}});
    GraphFlash flash;
    flash.resize(2);
    QCOMPARE(one.step(16, &flash), QList<int>{});
    QCOMPARE(one.pulses().size(), 1);
    const GraphPulses::Pulse first = one.pulses().first();
    QVERIFY(first.v >= 0.004 && first.v <= 0.010);
    QCOMPARE(first.t, 0.0);
    const int end = one.to(first);
    const int frames = int(std::ceil(1.0 / (first.v * 16 * 0.06) - 1e-9));
    for (int k = 1; k < frames; ++k) {
        QCOMPARE(one.step(16, &flash), QList<int>{});
        QCOMPARE(flash.at(end), 0.0);
    }
    const double before = one.pulses().first().t;
    QVERIFY(near(before, (frames - 1) * first.v * 16 * 0.06, 1e-9));
    QCOMPARE(one.step(16, &flash), QList<int>{end});
    QCOMPARE(flash.at(end), 0.7);
    QCOMPARE(one.pulses().size(), 1); // a new one started
    flash.fade(100);
    QVERIFY(near(flash.at(end), 0.7 - 0.22, 1e-12));
    flash.fade(1000);
    QCOMPARE(flash.at(end), 0.0);

    // 2,599 edges: 90 at once, every speed in range.
    QList<QPair<int, int>> edges;
    const QList<int> parents = bigParents();
    for (int i = 1; i < parents.size(); ++i)
        edges.append({i, parents[i]});
    GraphPulses many;
    many.setEdges(edges);
    many.step(16);
    QCOMPARE(many.pulses().size(), 90);
    for (int k = 0; k < 600; ++k) {
        many.step(16);
        QCOMPARE(many.pulses().size(), 90);
    }
    for (const GraphPulses::Pulse &p : many.pulses())
        QVERIFY(p.v >= 0.004 && p.v <= 0.010 && p.t >= 0 && p.t < 1);

    // 50 edges: 50 pulses, one on each edge.
    GraphPulses few;
    few.setEdges(edges.mid(0, 50));
    few.step(16);
    QCOMPARE(few.pulses().size(), 50);
    QSet<int> on;
    for (const GraphPulses::Pulse &p : few.pulses())
        on.insert(p.edge);
    QCOMPARE(on.size(), 50);
}

void GraphTests::graphSkyLikeTheSite()
{
    GraphSky sky;
    sky.build(1000, 1000);
    QCOMPARE(sky.stars(0).size(), 220);
    QCOMPARE(sky.stars(1).size(), 100);
    QCOMPARE(sky.stars(2).size(), 35);
    const double radii[3][2] = {{0.35, 0.8}, {0.6, 1.15}, {0.9, 1.7}};
    int white = 0, total = 0;
    for (int layer = 0; layer < 3; ++layer) {
        QSet<int> groups;
        for (const GraphSky::Star &s : sky.stars(layer)) {
            QVERIFY2(s.r >= radii[layer][0] && s.r <= radii[layer][1], qPrintable(describe(s.r)));
            QVERIFY(s.x >= 0 && s.x <= 1 && s.y >= 0 && s.y <= 1);
            QVERIFY(s.tint >= 0 && s.tint < 4);
            groups.insert(s.group);
            white += s.tint == GraphSky::White ? 1 : 0;
            ++total;
        }
        QCOMPARE(groups, (QSet<int>{0, 1, 2, 3}));
    }
    QVERIFY2(near(double(white) / total, 0.78, 0.07), qPrintable(u"%1 of %2 white"_s.arg(white).arg(total)));
    // The same sky every time.
    GraphSky again;
    again.build(1000, 1000);
    QCOMPARE(again.stars(2).first().x, sky.stars(2).first().x);

    // Twinkle, as the site's Universe.astro has it (v0.9; see graphTwinkleExactlyLikeTheSite):
    // 0.7 + 0.3 sin(t * 0.0011 * (1 + group * 0.37) + group * 1.9 + layer).
    QCOMPARE(GraphSky::twinkle(0, 0, 0), 0.7);
    QVERIFY(near(GraphSky::twinkle(1000, 1, 0), 0.7 + 0.3 * std::sin(1.1 * 1.37 + 1.9), 1e-12));
    QVERIFY(near(GraphSky::twinkle(5000, 0, 2), 0.7 + 0.3 * std::sin(5.5 + 2), 1e-12));

    // Parallax: each layer moves with the turn by its depth (0.12, 0.4, 0.9).
    QPointF by = sky.shift(0, 1, 0);
    QVERIFY(near(by.x(), -14.4, 1e-9) && near(by.y(), 0, 1e-12));
    by = sky.shift(2, 1, 0);
    QVERIFY(near(by.x(), -108, 1e-9));
    by = sky.shift(1, 0, 1);
    QVERIFY(near(by.x(), 0, 1e-12) && near(by.y(), 60, 1e-9));
    const QPointF still = sky.position(2, 0, 0, 0);
    const QPointF turned = sky.position(2, 0, 0.1, 0);
    const double moved = std::fmod(still.x() - turned.x() + 1000, 1000);
    QVERIFY2(near(moved, 10.8, 1e-6), qPrintable(describe(moved)));
    for (int i = 0; i < sky.stars(0).size(); ++i) {
        const QPointF at = sky.position(0, i, 37.5, -2);
        QVERIFY(at.x() >= 0 && at.x() < 1000 && at.y() >= 0 && at.y() < 1000);
    }

    // Shooting stars: the first within 2.5..5.5 s, then every 7..19 s.
    QList<double> starts;
    double t = 0;
    for (int frame = 0; frame < 600000 / 16; ++frame) {
        t += 16;
        if (sky.step(16))
            starts.append(t);
    }
    QVERIFY(!starts.isEmpty());
    QVERIFY2(starts.first() >= 2500 && starts.first() <= 5500 + 16, qPrintable(describe(starts.first())));
    for (qsizetype k = 1; k < starts.size(); ++k) {
        const double gap = starts[k] - starts[k - 1];
        QVERIFY2(gap >= 7000 && gap <= 19000 + 16, qPrintable(describe(gap)));
    }
    QVERIFY2(starts.size() >= 31 && starts.size() <= 86, qPrintable(QString::number(starts.size())));
    QVERIFY(sky.meteors().size() <= 1);
}

void GraphTests::graphLabelsLikeTheSite()
{
    using namespace GraphLook;
    QCOMPARE(labelMinSize(1.6), 200.0);
    QVERIFY(near(labelMinSize(2.6), 110, 1e-9));
    QCOMPARE(labelMinSize(5), 40.0);
    QCOMPARE(labelAlpha(1.5), 0.0);
    QVERIFY(near(labelAlpha(2.0), 0.6 * 0.85, 1e-9));
    QCOMPARE(labelAlpha(3), 0.85);

    // 40 big nodes in a column 20 px apart: at most 36, biggest first.
    QList<LabelCandidate> nodes;
    for (int k = 0; k < 40; ++k)
        nodes.append({100, 20.0 + k * 20, 1.0, 200 + k, u"n%1"_s.arg(k)});
    QCOMPARE(placeLabels(nodes, 1.59, 1000, 1000).size(), 0);
    QList<Label> labels = placeLabels(nodes, 3, 1000, 1000);
    QCOMPARE(labels.size(), 36);
    QCOMPARE(labels.first().text, u"n39"_s);
    QCOMPARE(labels.first().x, 106.0);
    QCOMPARE(labels.first().y, nodes[39].y);
    QCOMPARE(labels.last().text, u"n4"_s);
    QCOMPARE(labels.first().colour.name(), u"#c0caf5"_s);
    QCOMPARE(labels.first().alpha, 0.85);

    // Overlaps, colours and the rules for what is labelled.
    const QList<LabelCandidate> mixed{
        {100, 100, 1.0, 255, u"big"_s},      // labelled, bright
        {104, 105, 1.0, 250, u"under"_s},    // its box overlaps big's: left out
        {100, 200, 1.0, 171, u"bright"_s},   // size > 170: bright
        {100, 300, 1.0, 170, u"dim"_s},      // 170: dim
        {100, 400, 0.9, 255, u"behind"_s},   // too far back
        {-5, 500, 1.0, 255, u"off"_s},       // off the view
        {100, 600, 1.0, 39, u"small"_s},     // below 40 even zoomed in all the way
        {100, 700, 1.0, 255, u"hidden"_s, false},
    };
    labels = placeLabels(mixed, 7, 1000, 1000);
    QStringList texts;
    for (const Label &l : labels)
        texts.append(l.text);
    QCOMPARE(texts, (QStringList{u"big"_s, u"bright"_s, u"dim"_s}));
    QCOMPARE(labels[1].colour.name(), u"#c0caf5"_s);
    QCOMPARE(labels[2].colour.name(), u"#7f87a5"_s);
    // At zoom 2 only sizes of at least 164 are labelled.
    labels = placeLabels(mixed, 2, 1000, 1000);
    texts.clear();
    for (const Label &l : labels)
        texts.append(l.text);
    QCOMPARE(texts, (QStringList{u"big"_s, u"bright"_s, u"dim"_s}));
    labels = placeLabels(mixed, 2.2, 1000, 1000);
    QCOMPARE(labels.size(), 3);
    labels = placeLabels(mixed, 1.9, 1000, 1000);
    QCOMPARE(labels.size(), 1); // 173 at least: bright (171) and dim (170) are left out

    // The hover tooltip: "name · N files".
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(u"root"_s);
    for (int k = 0; k < 3; ++k)
        QVERIFY(makeFile(root + u"/docs/d%1.txt"_s.arg(k)));
    GraphBuilder builder;
    QVERIFY(build(builder, root, 5000) >= 0);
    QCOMPARE(builder.label(builder.tree().indexOf(root + u"/docs"_s)), u"docs · 3 files"_s);
}

void GraphTests::graphGrowsOverTime()
{
    QCOMPARE(GrowthReplay::DurationMs, 14000.0);
    QCOMPARE(GrowthReplay::ease(0), 0.0);
    QCOMPARE(GrowthReplay::ease(0.25), 0.125);
    QCOMPARE(GrowthReplay::ease(0.5), 0.5);
    QCOMPARE(GrowthReplay::ease(0.75), 0.875);
    QCOMPARE(GrowthReplay::ease(1), 1.0);

    // A repository with one file added every 30 days from 2020-01-01 12:00 UTC: a/1.txt, a/2.txt,
    // b/3.txt, b/4.txt, c/5.txt.
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(u"repo"_s);
    QVERIFY(QDir().mkpath(root));
    QVERIFY(git(root, {u"init"_s, u"-q"_s}));
    const qint64 base = 1577880000; // 2020-01-01T12:00:00Z
    const qint64 day = 86400;
    const QStringList files{u"a/1.txt"_s, u"a/2.txt"_s, u"b/3.txt"_s, u"b/4.txt"_s, u"c/5.txt"_s};
    for (int k = 0; k < files.size(); ++k) {
        QVERIFY(makeFile(root + u'/' + files[k]));
        QVERIFY(git(root, {u"add"_s, files[k]}));
        QVERIFY(git(root, {u"commit"_s, u"-q"_s, u"-m"_s, u"add %1"_s.arg(files[k])}, base + k * 30 * day));
    }
    // Their modified times are now; git's dates must win.
    const GraphTree tree = GraphTree::walk(root, false);
    QCOMPARE(tree.size(), 9);
    const QList<qint64> born = GraphHistory::bornTimes(tree);
    QVERIFY(GraphHistory::lastUsedGit());
    const auto bornOf = [&](const QString &relative) { return born.at(tree.indexOf(root + relative)); };
    QCOMPARE(born.at(0), base);
    QCOMPARE(bornOf(u"/a"_s), base);
    QCOMPARE(bornOf(u"/a/1.txt"_s), base);
    QCOMPARE(bornOf(u"/a/2.txt"_s), base + 30 * day);
    QCOMPARE(bornOf(u"/b"_s), base + 60 * day);
    QCOMPARE(bornOf(u"/b/3.txt"_s), base + 60 * day);
    QCOMPARE(bornOf(u"/b/4.txt"_s), base + 90 * day);
    QCOMPARE(bornOf(u"/c"_s), base + 120 * day);
    QCOMPARE(bornOf(u"/c/5.txt"_s), base + 120 * day);

    GrowthReplay replay;
    GraphFlash flash;
    flash.resize(tree.size());
    QVERIFY(replay.shown(5));
    replay.start(born);
    QCOMPARE(replay.first(), base);
    QCOMPARE(replay.last(), base + 120 * day);
    QCOMPARE(replay.seek(0, &flash).size(), 3); // root, a, a/1.txt
    QCOMPARE(replay.shownCount(), 3);
    QCOMPARE(replay.caption(), u"2020-01"_s);
    QCOMPARE(flash.at(0), 1.0);
    flash.fade(1000);
    QCOMPARE(replay.seek(3500, &flash).size(), 0); // 15 days in
    QCOMPARE(replay.caption(), u"2020-01"_s);

    // Halfway through (7 s): exactly the nodes at or before the median time (2020-03-01 12:00).
    QList<qint64> sorted = born;
    std::sort(sorted.begin(), sorted.end());
    const qint64 median = sorted.at(sorted.size() / 2);
    QCOMPARE(median, base + 60 * day);
    const QList<int> appeared = replay.seek(7000, &flash);
    QCOMPARE(replay.now(), double(median));
    int atOrBefore = 0;
    for (int i = 0; i < tree.size(); ++i) {
        QCOMPARE(replay.shown(i), born.at(i) <= median);
        atOrBefore += born.at(i) <= median ? 1 : 0;
    }
    QCOMPARE(atOrBefore, 6);
    QCOMPARE(replay.shownCount(), 6);
    QCOMPARE(appeared.size(), 3); // a/2.txt, b, b/3.txt
    for (int i : appeared)
        QCOMPARE(flash.at(i), 1.0);
    QCOMPARE(flash.at(0), 0.0);
    QCOMPARE(replay.caption(), u"2020-03"_s);

    // Stepping on in frames to the end: everything shown, in April 2020 (base + 120 days).
    int frames = 0;
    while (!replay.finished() && frames < 2000) {
        replay.step(16, &flash);
        ++frames;
    }
    QVERIFY(replay.finished());
    QVERIFY2(frames >= 437 && frames <= 438, qPrintable(QString::number(frames)));
    QCOMPARE(replay.shownCount(), 9);
    QCOMPARE(replay.caption(), u"2020-04"_s);
    replay.stop();
    QVERIFY(replay.caption().isEmpty());
    QVERIFY(replay.shown(8));

    // Outside a repository: each file's older of birth and modified time, a folder its oldest.
    const QString plain = scratch.filePath(u"plain"_s);
    QVERIFY(makeFile(plain + u"/x/old.txt"_s));
    QVERIFY(makeFile(plain + u"/x/older.txt"_s));
    const auto setModified = [](const QString &path, qint64 when) {
        QFile file(path);
        return file.open(QIODevice::ReadWrite)
               && file.setFileTime(QDateTime::fromSecsSinceEpoch(when, QTimeZone::UTC), QFileDevice::FileModificationTime);
    };
    const qint64 june = 1559390400;  // 2019-06-01T12:00:00Z
    const qint64 may = 1556712000;   // 2019-05-01T12:00:00Z
    QVERIFY(setModified(plain + u"/x/old.txt"_s, june));
    QVERIFY(setModified(plain + u"/x/older.txt"_s, may));
    const GraphTree loose = GraphTree::walk(plain, false);
    const QList<qint64> times = GraphHistory::bornTimes(loose);
    QVERIFY(!GraphHistory::lastUsedGit());
    QCOMPARE(times.at(loose.indexOf(plain + u"/x/old.txt"_s)), june);
    QCOMPARE(times.at(loose.indexOf(plain + u"/x/older.txt"_s)), may);
    QCOMPARE(times.at(loose.indexOf(plain + u"/x"_s)), may);
    QCOMPARE(times.at(0), may);
}

void GraphTests::graphTwinkleExactlyLikeTheSite()
{
    // 0.7 + 0.3 sin(1000 * 0.0011 * 1.74 + 3.8 + 1) = 0.7 + 0.3 sin(6.714).
    const double t = GraphSky::twinkle(1000, 2, 1);
    QVERIFY2(near(t, 0.82528, 0.00001), qPrintable(describe(t)));
}

void GraphTests::graphLabelNotUnderTooltip()
{
    using namespace GraphLook;
    // A tree of 1,000 nodes: the folder, 100 folders and 899 files in them (9 in each, 8 in the
    // last), so that every folder is big enough to be labelled at zoom 3 and 36 labels show.
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString root = scratch.filePath(u"thousand"_s);
    for (int folder = 0; folder < 100; ++folder) {
        const QString dir = root + u"/d%1"_s.arg(folder, 2, 10, QLatin1Char('0'));
        QVERIFY(QDir().mkpath(dir));
        for (int file = 0; file < (folder == 99 ? 8 : 9); ++file)
            QVERIFY(makeFile(dir + u"/f%1.txt"_s.arg(file)));
    }
    GraphBuilder builder;
    QVERIFY(build(builder, root, 30000) >= 0);
    QCOMPARE(builder.count(), 1000);

    const GraphTree &tree = builder.tree();
    QStringList names;
    for (int i = 0; i < builder.count(); ++i)
        names.append(tree.node(i).name);
    GraphFrame frame;
    frame.setGraph(builder.sitePositions(), builder.sizes(), builder.groups(), names, tree.edges());

    // A wide window, 1920 x 800, as the graph opens: turned 0.6 and tipped -0.25, zoomed to 3.
    GraphFrame::View view;
    view.width = 1920;
    view.height = 800;
    view.rotY = 0.6;
    view.rotX = -0.25;
    view.zoom = 3;
    frame.prepare(view, nullptr, nullptr, nullptr, nullptr);
    const QList<Label> all = frame.labels();
    QCOMPARE(all.size(), 36);
    QVERIFY(frame.tooltip().isEmpty());

    // Hovering each labelled node in turn: its tooltip is shown (the size the view gives it: the
    // text, 8 px either side, 4 px above and below), its own label is not, and no label's box
    // overlaps the tooltip's.
    int wouldHaveOverlapped = 0;
    for (const Label &hovered : all) {
        const QString text = builder.label(hovered.node);
        view.tooltipNode = hovered.node;
        view.tooltipWidth = monospaceWidth(text) + 16;
        view.tooltipHeight = 13 + 8;
        frame.prepare(view, nullptr, nullptr, nullptr, nullptr);
        const Box tip = frame.tooltip();
        QVERIFY2(!tip.isEmpty(), qPrintable(text));
        QCOMPARE(tip.left, std::max(0.0, std::min(view.width - view.tooltipWidth, hovered.x - LabelGap + TooltipGap)));
        for (const Label &l : all) {
            if (l.node != hovered.node && labelBox(l, monospaceWidth(l.text)).overlaps(tip))
                ++wouldHaveOverlapped;
        }
        for (const Label &l : frame.labels()) {
            QVERIFY2(l.node != hovered.node, qPrintable(u"%1 still labelled under its tooltip"_s.arg(text)));
            QVERIFY2(!labelBox(l, monospaceWidth(l.text)).overlaps(tip),
                     qPrintable(u"label %1 under the tooltip of %2"_s.arg(l.text, text)));
        }
    }
    // The check bites: placed without keeping clear, labels would have been under tooltips.
    QVERIFY2(wouldHaveOverlapped > 0, "no tooltip ever covered a label");
}

QTEST_GUILESS_MAIN(GraphTests)
#include "graph_tests.moc"
