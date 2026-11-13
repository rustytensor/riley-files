// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The graph view in the real window, drawn with real OpenGL: Mesa's software renderer through
// this test's own window system (glplatform.cpp), as a machine with no GPU has it. The keys move
// through the graph and Return opens a folder in the list; the view draws a starry, coloured
// frame with no warning and labels the node under the mouse; and where 3D cannot start, it says
// so while every other view goes on working.

#include "browser.h"
#include "uiharness.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>
#include <QtPlugin>

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
    QTRY_VERIFY(ui.view(u"graphView3D"_s));
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
    QVERIFY(!ui.item(u"graphView3D"_s));

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
