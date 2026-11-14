// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The graph view in the real window, drawn with real OpenGL: Mesa's software renderer through
// this test's own window system (glplatform.cpp), as a machine with no GPU has it. The keys move
// through the graph and Return opens a folder in the list; the view draws a starry, coloured
// frame with no warning and labels the node under the mouse; and where 3D cannot start, it says
// so while every other view goes on working. Then the look of servorra.com's (v0.8): "Watch it
// grow" from the key and the button, and a frame of 2,600 nodes made in time, with the window
// never stalling.

#include "browser.h"
#include "graphcanvas.h"
#include "uiharness.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMutex>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>
#include <QTimeZone>
#include <QtMath>
#include <QtPlugin>

#include <algorithm>

Q_IMPORT_PLUGIN(RileyTestGlPlugin)

using namespace Qt::StringLiterals;

namespace {

QMutex messagesLock;
QStringList warningsSeen;
QtMessageHandler defaultHandler = nullptr;

// Every warning while the tests run, from Qt or from the program, whatever its category.
void keepEveryWarning(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        QMutexLocker locker(&messagesLock);
        warningsSeen.append(message);
    }
    if (defaultHandler)
        defaultHandler(type, context, message);
}

// The fixture: root/a/a1, root/a/a2, root/b/b1 (folders), root/f1.txt, root/f2.txt,
// root/a/a1/x.txt.
bool makeFixture(const UiHarness &ui)
{
    QDir root(ui.root());
    return root.mkpath(u"a/a1"_s) && root.mkpath(u"a/a2"_s) && root.mkpath(u"b/b1"_s)
           && ui.makeFile(u"f1.txt"_s) && ui.makeFile(u"f2.txt"_s) && ui.makeFile(u"a/a1/x.txt"_s);
}

QString selectedPath(const UiHarness &ui)
{
    Riley::GraphBuilder *graph = ui.browser().graph();
    return graph->pathOf(ui.browser().graphNode());
}

QString labelText(const UiHarness &ui)
{
    QQuickItem *label = ui.item(u"graphLabel"_s);
    return label && label->isVisible() ? label->property("text").toString() : QString();
}

int distinctColours(const QImage &image)
{
    QSet<QRgb> colours;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            colours.insert(line[x]);
    }
    return int(colours.size());
}

// Runs git in folder as a fixed person, the commit dated `when` (seconds since 1970, UTC) when given.
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

GraphCanvas *canvasOf(const UiHarness &ui)
{
    return qobject_cast<GraphCanvas *>(ui.view(u"graphCanvas"_s));
}

QString captionText(const UiHarness &ui)
{
    QQuickItem *caption = ui.item(u"graphGrowthCaption"_s);
    return caption && caption->isVisible() ? caption->property("text").toString() : QString();
}

// The graph's part of the window as drawn now.
QImage grabGraph(const UiHarness &ui)
{
    QQuickItem *graph = ui.view(u"graph"_s);
    if (!graph)
        return {};
    const QRectF area = graph->mapRectToScene(QRectF(0, 0, graph->width(), graph->height()));
    const QImage frame = ui.window()->grabWindow();
    const qreal ratio = frame.devicePixelRatio();
    return frame.copy(QRect(qRound(area.x() * ratio), qRound(area.y() * ratio), qRound(area.width() * ratio),
                            qRound(area.height() * ratio)));
}

// Runs the program's environment change for one test and undoes it after.
struct EnvironmentFor
{
    EnvironmentFor(const char *name, const QByteArray &value)
        : m_name(name)
    {
        qputenv(name, value);
    }
    ~EnvironmentFor() { qunsetenv(m_name); }
    const char *m_name;
};

} // namespace

class GraphUiTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void uiGraphNavigates();
    void graphRendersSomething();
    void graphSaysWhenItCannotDraw();
    void graphGrowsOverTime();
    void graphDrawsFast();
    void graphStaysInsideItsPane();
};

void GraphUiTests::init()
{
    QMutexLocker locker(&messagesLock);
    warningsSeen.clear();
}

void GraphUiTests::uiGraphNavigates()
{
    UiHarness ui;
    QVERIFY(makeFixture(ui));
    const QString root = ui.root();
    QVERIFY(ui.start({root}));
    QTRY_VERIFY(ui.view(u"list"_s));

    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QCOMPARE(ui.browser().viewName(), u"graph"_s);
    QTRY_COMPARE(ui.browser().graph()->count(), 9);
    QTRY_COMPARE(selectedPath(ui), root);

    ui.press(Qt::Key_Down);
    QCOMPARE(selectedPath(ui), root + u"/a"_s);
    QTRY_COMPARE(labelText(ui), u"a · 1 file"_s);
    ui.press(Qt::Key_Right);
    QCOMPARE(selectedPath(ui), root + u"/b"_s);
    ui.press(Qt::Key_Up);
    QCOMPARE(selectedPath(ui), root);

    // Return on folder a: a in the list view.
    ui.press(Qt::Key_Down);
    QCOMPARE(selectedPath(ui), root + u"/a"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(ui.view(u"list"_s));
    QVERIFY(!ui.view(u"graph"_s));
    QCOMPARE(ui.browser().folder(), root + u"/a"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"a1"_s, u"a2"_s}));

    // Ctrl+2 from the graph, and Escape, go back to the list.
    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QTRY_COMPARE(ui.browser().graph()->count(), 4); // a, a1, a2, x.txt
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"list"_s));
    QVERIFY(!ui.view(u"graph"_s));
    QCOMPARE(ui.browser().viewName(), u"list"_s);
    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(ui.view(u"list"_s));
    QCOMPARE(ui.browser().folder(), root + u"/a"_s);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void GraphUiTests::graphRendersSomething()
{
    UiHarness ui;
    QVERIFY(makeFixture(ui));
    const QString root = ui.root();
    QVERIFY(ui.start({root}));
    QTRY_VERIFY(ui.view(u"list"_s));
    // Drawn through OpenGL (llvmpipe here), not in software.
    QCOMPARE(ui.window()->rendererInterface()->graphicsApi(), QSGRendererInterface::OpenGL);

    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QTRY_COMPARE(ui.browser().graph()->count(), 9);
    QTRY_VERIFY(ui.view(u"graphCanvas"_s));
    QVERIFY(!ui.view(u"graphNo3D"_s));
    QTest::qWait(400); // a few frames

    QQuickItem *graph = ui.view(u"graph"_s);
    const QRectF area = graph->mapRectToScene(QRectF(0, 0, graph->width(), graph->height()));
    const QImage frame = ui.window()->grabWindow();
    QVERIFY(!frame.isNull());
    const qreal ratio = frame.devicePixelRatio();
    const QRect inGraph(qRound(area.x() * ratio), qRound(area.y() * ratio), qRound(area.width() * ratio),
                        qRound(area.height() * ratio));
    // A picture of it, for people to look at, when RILEY_SHOTS names a folder.
    if (const QString shots = qEnvironmentVariable("RILEY_SHOTS"); !shots.isEmpty())
        frame.save(QDir(shots).filePath(u"graph.png"_s));
    const int colours = distinctColours(frame.copy(inGraph));
    QVERIFY2(colours >= 8, qPrintable(QString::number(colours)));

    // Hovering node a shows its label.
    QQuickItem *scene = ui.view(u"graphScene"_s);
    QVERIFY(scene);
    const int a = ui.browser().graph()->indexOf(root + u"/a"_s);
    QVERIFY(a > 0);
    QVariant point;
    QVERIFY(QMetaObject::invokeMethod(scene, "screenPointOf", Q_RETURN_ARG(QVariant, point), Q_ARG(QVariant, a)));
    const QPointF at = scene->mapToScene(point.toPointF());
    QVERIFY2(area.contains(at), qPrintable(u"%1,%2"_s.arg(at.x()).arg(at.y())));
    QTest::mouseMove(ui.window(), at.toPoint());
    QTRY_COMPARE(ui.browser().graphHover(), a);
    QTRY_COMPARE(labelText(ui), u"a · 1 file"_s);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
    QMutexLocker locker(&messagesLock);
    QVERIFY2(warningsSeen.isEmpty(), qPrintable(warningsSeen.join(u'\n')));
}

void GraphUiTests::graphSaysWhenItCannotDraw()
{
    const EnvironmentFor no3D("RILEY_FILES_NO_3D", "1");
    UiHarness ui;
    QVERIFY(makeFixture(ui));
    QVERIFY(ui.start({ui.root()}));
    QTRY_VERIFY(ui.view(u"list"_s));

    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QQuickItem *message = ui.view(u"graphNo3D"_s);
    QVERIFY(message);
    QCOMPARE(message->property("text").toString(), u"The graph needs 3D graphics."_s);
    QVERIFY(!ui.item(u"graphCanvas"_s));

    // Every other view still works.
    const QStringList names{u"a"_s, u"b"_s, u"f1.txt"_s, u"f2.txt"_s};
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"grid"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"grid"_s)), names);
    ui.press(Qt::Key_3, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"columns"_s));
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"list"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), names);
    QVERIFY(!ui.view(u"graph"_s));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void GraphUiTests::graphGrowsOverTime()
{
    // A repository with one file added every 30 days from 2020-01-01 12:00 UTC: a/1.txt, a/2.txt,
    // b/3.txt, b/4.txt, c/5.txt; nine nodes with the folders and the root.
    UiHarness ui;
    const QString root = ui.root();
    QVERIFY(git(root, {u"init"_s, u"-q"_s}));
    const qint64 base = 1577880000; // 2020-01-01T12:00:00Z
    const qint64 day = 86400;
    const QStringList files{u"a/1.txt"_s, u"a/2.txt"_s, u"b/3.txt"_s, u"b/4.txt"_s, u"c/5.txt"_s};
    for (int k = 0; k < files.size(); ++k) {
        QVERIFY(ui.makeFile(files[k]));
        QVERIFY(git(root, {u"add"_s, files[k]}));
        QVERIFY(git(root, {u"commit"_s, u"-q"_s, u"-m"_s, u"add %1"_s.arg(files[k])}, base + k * 30 * day));
    }
    QVERIFY(ui.start({root}));
    QTRY_VERIFY(ui.view(u"list"_s));
    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QTRY_COMPARE(ui.browser().graph()->count(), 9);
    QTRY_VERIFY(canvasOf(ui));
    GraphCanvas *canvas = canvasOf(ui);
    QTRY_COMPARE(canvas->frame().drawnNodes(), 9);
    QVERIFY(!canvas->growing());
    QCOMPARE(captionText(ui), QString());

    // G starts it: the first month, and only what was there then (the root, a, a/1.txt).
    ui.press(Qt::Key_G);
    QVERIFY(canvas->growing());
    QCOMPARE(captionText(ui), u"2020-01"_s);
    QCOMPARE(canvas->shownCount(), 3);
    QCOMPARE(canvas->frame().drawnNodes(), 3);
    QCOMPARE(canvas->frame().drawnEdges(), 2);

    // Halfway (7 s of 14): exactly the nodes at or before the median time, 2020-03-01 12:00.
    QVERIFY(QMetaObject::invokeMethod(canvas, "seekGrowth", Q_ARG(double, 7000)));
    QCOMPARE(canvas->shownCount(), 6);
    QCOMPARE(canvas->frame().drawnNodes(), 6);
    QCOMPARE(captionText(ui), u"2020-03"_s);
    Riley::GraphBuilder *graph = ui.browser().graph();
    const qint64 median = base + 60 * day;
    const QStringList there{root, root + u"/a"_s, root + u"/a/1.txt"_s, root + u"/a/2.txt"_s, root + u"/b"_s,
                            root + u"/b/3.txt"_s};
    for (int i = 0; i < graph->count(); ++i) {
        QCOMPARE(graph->bornTimes().at(i) <= median, there.contains(graph->pathOf(i)));
        QVERIFY2(canvas->frame().drawn(i) == there.contains(graph->pathOf(i)), qPrintable(graph->pathOf(i)));
    }

    // The button starts it over.
    QQuickItem *button = ui.view(u"graphGrow"_s);
    QVERIFY(button);
    const QPointF middle = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier, middle.toPoint());
    QTRY_COMPARE(canvas->shownCount(), 3);
    QCOMPARE(captionText(ui), u"2020-01"_s);

    // At the end everything is there, in April 2020; a moment later the graph is whole again.
    QVERIFY(QMetaObject::invokeMethod(canvas, "seekGrowth", Q_ARG(double, 14000)));
    QCOMPARE(canvas->shownCount(), 9);
    QCOMPARE(captionText(ui), u"2020-04"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->growing(), 5000);
    QCOMPARE(captionText(ui), QString());
    QCOMPARE(canvas->frame().drawnNodes(), 9);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
    QMutexLocker locker(&messagesLock);
    QVERIFY2(warningsSeen.isEmpty(), qPrintable(warningsSeen.join(u'\n')));
}

void GraphUiTests::graphDrawsFast()
{
    // 2,600 nodes: the folder, 100 folders below it and 2,499 files in them (2,599 edges).
    UiHarness ui;
    const QString root = ui.root();
    int made = 0;
    for (int folder = 0; folder < 100; ++folder) {
        const QString dir = u"d%1"_s.arg(folder, 3, 10, QLatin1Char('0'));
        QVERIFY(QDir(root).mkpath(dir));
        for (int file = 0; file < 25 && made < 2499; ++file, ++made)
            QVERIFY(ui.makeFile(dir + u"/f%1.txt"_s.arg(file)));
    }
    QVERIFY(ui.start({root}));
    QTRY_VERIFY(ui.view(u"list"_s));
    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QTRY_COMPARE_WITH_TIMEOUT(ui.browser().graph()->count(), 2600, 60000);
    QTRY_VERIFY(canvasOf(ui));
    GraphCanvas *canvas = canvasOf(ui);
    // Zoomed in, so the labels are made too: five notches of the wheel.
    for (int notch = 0; notch < 5; ++notch) {
        const QPointF middle = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2));
        QWheelEvent wheel(middle, ui.window()->mapToGlobal(middle), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(ui.window(), &wheel);
    }
    QTRY_VERIFY(canvas->zoom() > 2.3);
    QTRY_COMPARE(canvas->frame().drawnPulses(), 90);

    QElapsedTimer clock;
    clock.start();
    QMutex swapsLock;
    QList<qint64> swaps;
    const auto swapped = connect(ui.window(), &QQuickWindow::frameSwapped, ui.window(), [&] {
        QMutexLocker locker(&swapsLock);
        swaps.append(clock.nsecsElapsed());
    }, Qt::DirectConnection);
    canvas->clearPrepareTimes();
    QTRY_VERIFY_WITH_TIMEOUT(canvas->prepareTimes().size() >= 120, 60000);
    disconnect(swapped);

    // Everything was in the frames: all nodes and edges, 90 pulses, the sky and the labels.
    QCOMPARE(canvas->frame().drawnNodes(), 2600);
    QCOMPARE(canvas->frame().drawnEdges(), 2599);
    QCOMPARE(canvas->frame().drawnPulses(), 90);
    QVERIFY(canvas->frame().drawnStars() > 100);
    QVERIFY(!canvas->frame().labels().isEmpty());

    QList<double> times = canvas->prepareTimes().mid(0, 120);
    std::sort(times.begin(), times.end());
    const double median = (times.at(59) + times.at(60)) / 2;
    QVERIFY2(median < 8.0, qPrintable(u"median %1 ms"_s.arg(median)));

    // The window kept drawing: no gap between frames over 50 ms (the first few left out, while
    // the drawing starts).
    QMutexLocker locker(&swapsLock);
    QVERIFY2(swaps.size() >= 60, qPrintable(QString::number(swaps.size())));
    double longest = 0;
    for (qsizetype k = 5; k < swaps.size(); ++k)
        longest = std::max(longest, double(swaps.at(k) - swaps.at(k - 1)) / 1e6);
    qInfo().noquote() << u"frame made in %1 ms (median of 120), longest gap between frames %2 ms"_s.arg(median).arg(longest);
    QVERIFY2(longest < 50.0, qPrintable(u"a frame took %1 ms (median %2 ms)"_s.arg(longest).arg(median)));
    locker.unlock();

    // Pictures for people to look at, when RILEY_SHOTS names a folder: a tree of 1,000 nodes
    // made over two years, whole and halfway through "Watch it grow".
    if (const QString shots = qEnvironmentVariable("RILEY_SHOTS"); !shots.isEmpty()) {
        const QString thousand = ui.scratch() + u"/thousand"_s;
        const qint64 start = 1609502400; // 2021-01-01T12:00:00Z
        int k = 0;
        for (int folder = 0; folder < 40 && k < 959; ++folder) {
            const QString dir = thousand + u"/%1%2"_s.arg(QChar(u'a' + folder % 26)).arg(folder);
            QVERIFY(QDir().mkpath(dir));
            for (int file = 0; (file < 4 + (folder * 7) % 41 || folder == 39) && k < 959; ++file, ++k) {
                QFile f(dir + u"/f%1.txt"_s.arg(file));
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.setFileTime(QDateTime::fromSecsSinceEpoch(start + qint64(k) * 66000, QTimeZone::UTC),
                              QFileDevice::FileModificationTime);
            }
        }
        ui.press(Qt::Key_Escape);
        QTRY_VERIFY(ui.view(u"list"_s));
        ui.browser().open(thousand);
        QTRY_COMPARE(ui.browser().folder(), thousand);
        ui.press(Qt::Key_4, Qt::ControlModifier);
        QTRY_VERIFY(ui.view(u"graph"_s));
        QTRY_COMPARE_WITH_TIMEOUT(ui.browser().graph()->count(), 1000, 60000);
        QTRY_VERIFY(canvasOf(ui));
        canvas = canvasOf(ui);
        QTest::qWait(1500);
        QVERIFY(grabGraph(ui).save(QDir(shots).filePath(u"graph-1000.png"_s)));
        canvas->watchItGrow();
        canvas->seekGrowth(7000);
        QTest::qWait(200);
        QVERIFY(grabGraph(ui).save(QDir(shots).filePath(u"graph-grow.png"_s)));
        canvas->motion().zoomBy(2.2);
        canvas->stopGrowth();
        QTest::qWait(1500);
        QVERIFY(grabGraph(ui).save(QDir(shots).filePath(u"graph-labels.png"_s)));
    }

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

// Marc 2026-11-14: "The 3D graph view in Riley Files goes outside of its window". Zoomed in, the graph's nodes, glows and
// labels land far past its pane; none of it may be drawn over the sidebar, the header or anything else. The window
// outside the graph's pane is grabbed at zoom 1 and again at the largest zoom: not one pixel there may change.
void GraphUiTests::graphStaysInsideItsPane()
{
    UiHarness ui;
    QVERIFY(makeFixture(ui));
    const QString root = ui.root();
    QVERIFY(ui.start({root}));
    QTRY_VERIFY(ui.view(u"list"_s));
    ui.press(Qt::Key_4, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"graph"_s));
    QTRY_COMPARE(ui.browser().graph()->count(), 9);
    QTRY_VERIFY(canvasOf(ui));
    GraphCanvas *canvas = canvasOf(ui);
    QTest::qWait(400);

    QQuickItem *graph = ui.view(u"graph"_s);
    const QRectF area = graph->mapRectToScene(QRectF(0, 0, graph->width(), graph->height()));
    QQuickItem *sidebar = ui.item(u"sidebar"_s);
    QVERIFY2(sidebar && sidebar->isVisible() && sidebar->width() > 50, "the sidebar must show beside the graph");

    const auto outside = [&](const QImage &frame) {
        QImage copy = frame.convertToFormat(QImage::Format_ARGB32);
        const qreal ratio = frame.devicePixelRatio();
        const QRect in(qFloor(area.x() * ratio), qFloor(area.y() * ratio), qCeil(area.width() * ratio),
                       qCeil(area.height() * ratio));
        for (int y = std::max(0, in.top()); y <= in.bottom() && y < copy.height(); ++y)
            for (int x = std::max(0, in.left()); x <= in.right() && x < copy.width(); ++x)
                copy.setPixel(x, y, 0);
        return copy;
    };
    const QImage before = outside(ui.window()->grabWindow());

    for (int notch = 0; notch < 14; ++notch) {
        const QPointF middle = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2));
        QWheelEvent wheel(middle, ui.window()->mapToGlobal(middle), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(ui.window(), &wheel);
    }
    QTRY_VERIFY(canvas->zoom() > 6.9);
    QTest::qWait(400);
    // Nodes now lie far outside the pane: without that, the check would prove nothing.
    QQuickItem *scene = ui.view(u"graphScene"_s);
    QVERIFY(scene);
    int nodesOutside = 0;
    for (int node = 0; node < ui.browser().graph()->count(); ++node) {
        QVariant point;
        QVERIFY(QMetaObject::invokeMethod(scene, "screenPointOf", Q_RETURN_ARG(QVariant, point), Q_ARG(QVariant, node)));
        nodesOutside += !area.contains(scene->mapToScene(point.toPointF()));
    }
    QVERIFY2(nodesOutside > 0, "no node lies outside the pane at this zoom");

    const QImage frame = ui.window()->grabWindow();
    const QImage after = outside(frame);
    QCOMPARE(after.size(), before.size());
    int changed = 0;
    for (int y = 0; y < after.height(); ++y)
        for (int x = 0; x < after.width(); ++x)
            changed += after.pixel(x, y) != before.pixel(x, y);
    if (const QString shots = qEnvironmentVariable("RILEY_SHOTS"); changed && !shots.isEmpty())
        frame.save(QDir(shots).filePath(u"graph-spill.png"_s));
    QCOMPARE(changed, 0);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

int main(int argc, char **argv)
{
    // Real OpenGL with no display: this test's own window system (see glplatform.cpp).
    qputenv("QT_QPA_PLATFORM", "rileytestgl");
    UiHarness::drawWithOpenGL();
    defaultHandler = qInstallMessageHandler(keepEveryWarning);
    QGuiApplication app(argc, argv);
    GraphUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "graphui_tests.moc"
