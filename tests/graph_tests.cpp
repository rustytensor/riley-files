// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The graph view's library on real files: the tree walked into nodes and edges, folders only for
// a large tree, and a layout that is the same every time, keeps nodes apart and is made off the
// UI thread.

#include "forcelayout.h"
#include "graphbuilder.h"
#include "graphtree.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <cmath>

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

QTEST_GUILESS_MAIN(GraphTests)
#include "graph_tests.moc"
