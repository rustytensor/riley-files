// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The main window as a person uses it: the real QML and C++, started on real folders and
// driven with keys. Expected results are the ones written down in the milestone's test list.

#include "browser.h"
#include "uiharness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

class UiTests : public QObject
{
    Q_OBJECT

private slots:
    void uiOpensAFolderGivenOnTheCommandLine();
    void uiKeyboardBrowses();
    void uiTreeKeys();
    void uiSwitchesViews();
    void uiColumnsBrowse();
    void uiGridMovesByRows();
    void uiStaysResponsiveOnALargeFolder();
};

void UiTests::uiOpensAFolderGivenOnTheCommandLine()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QCOMPARE(ui.browser().folder(), ui.root());
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void UiTests::uiKeyboardBrowses()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.makeFile(u".h"_s));
    QVERIFY(ui.start({ui.root()}));
    const QString zeta = QDir(ui.root()).filePath(u"Zeta"_s);

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QVERIFY(!ui.browser().tree());
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"Alpha"_s);

    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_Down);
    QCOMPARE(UiHarness::currentName(list), u"e.txt"_s);
    ui.press(Qt::Key_Up);
    QCOMPARE(UiHarness::currentName(list), u"Zeta"_s);

    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), zeta);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"z.txt"_s}));

    ui.press(Qt::Key_Backspace);
    QCOMPARE(ui.browser().folder(), ui.root());
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"Zeta"_s);

    // History: root, Zeta, root. Back is Zeta, forward is root again.
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(ui.browser().folder(), zeta);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"z.txt"_s}));
    ui.press(Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(ui.browser().folder(), ui.root());
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"Zeta"_s);

    ui.type(u"e"_s);
    QCOMPARE(UiHarness::currentName(list), u"e.txt"_s);

    ui.press(Qt::Key_H, Qt::ControlModifier);
    QTRY_COMPARE(UiHarness::shownNames(list).size(), 4);
    QTRY_VERIFY(UiHarness::shownNames(list).contains(u".h"_s));
    QCOMPARE(UiHarness::currentName(list), u"e.txt"_s);
    ui.press(Qt::Key_H, Qt::ControlModifier);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QCOMPARE(UiHarness::currentName(list), u"e.txt"_s);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void UiTests::uiTreeKeys()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    const QStringList top{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s};

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), top);
    QTRY_COMPARE(UiHarness::currentName(list), u"Alpha"_s);

    // Off by default: no row is pushed in.
    QVERIFY(!ui.browser().tree());
    QCOMPARE(UiHarness::shownInts(list, "indent"), (QList<int>{0, 0, 0}));

    ui.press(Qt::Key_T);
    QVERIFY(ui.browser().tree());

    ui.press(Qt::Key_Right);
    QTRY_COMPARE(UiHarness::shownNames(list),
                 (QStringList{u"Alpha"_s, u"Beta"_s, u"d.txt"_s, u"Zeta"_s, u"e.txt"_s}));
    QCOMPARE(ui.browser().folder(), ui.root());
    QCOMPARE(UiHarness::currentName(list), u"Alpha"_s);
    QCOMPARE(UiHarness::shownInts(list, "depth"), (QList<int>{0, 1, 1, 0, 0}));
    const QList<int> indents = UiHarness::shownInts(list, "indent");
    QVERIFY(indents.at(1) > indents.at(0));

    ui.press(Qt::Key_Right);
    QCOMPARE(UiHarness::currentName(list), u"Beta"_s);
    ui.press(Qt::Key_Right);
    QTRY_COMPARE(UiHarness::shownNames(list),
                 (QStringList{u"Alpha"_s, u"Beta"_s, u"c.txt"_s, u"d.txt"_s, u"Zeta"_s, u"e.txt"_s}));
    QCOMPARE(UiHarness::currentName(list), u"Beta"_s);
    ui.press(Qt::Key_Right);
    QCOMPARE(UiHarness::currentName(list), u"c.txt"_s);

    ui.press(Qt::Key_Left);
    QCOMPARE(UiHarness::currentName(list), u"Beta"_s);
    ui.press(Qt::Key_Left);
    QTRY_COMPARE(UiHarness::shownNames(list).size(), 5);
    QCOMPARE(UiHarness::currentName(list), u"Beta"_s);
    ui.press(Qt::Key_Left);
    QCOMPARE(UiHarness::currentName(list), u"Alpha"_s);
    ui.press(Qt::Key_Left);
    QTRY_COMPARE(UiHarness::shownNames(list), top);
    QCOMPARE(UiHarness::currentName(list), u"Alpha"_s);

    // Left on a closed folder at the top goes up to the folder that holds this one.
    ui.press(Qt::Key_Left);
    QCOMPARE(ui.browser().folder(), ui.scratch());
    QTRY_COMPARE(UiHarness::currentName(list), u"root"_s);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void UiTests::uiSwitchesViews()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    const QStringList top{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s};

    QVERIFY(ui.view(u"list"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), top);

    ui.press(Qt::Key_1, Qt::ControlModifier);
    QVERIFY(ui.view(u"grid"_s));
    QVERIFY(!ui.view(u"list"_s));
    QVERIFY(!ui.view(u"columns"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"grid"_s)), top);

    ui.press(Qt::Key_2, Qt::ControlModifier);
    QVERIFY(ui.view(u"list"_s));
    QVERIFY(!ui.view(u"grid"_s));
    QVERIFY(!ui.view(u"columns"_s));

    ui.press(Qt::Key_3, Qt::ControlModifier);
    QVERIFY(ui.view(u"columns"_s));
    QVERIFY(!ui.view(u"list"_s));
    QVERIFY(!ui.view(u"grid"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"column1"_s)), top);

    // A new window reading the same settings shows the view chosen last.
    QVERIFY(ui.start({ui.root()}));
    QVERIFY(ui.view(u"columns"_s));
    QVERIFY(!ui.view(u"list"_s));
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QVERIFY(ui.start({ui.root()}));
    QVERIFY(ui.view(u"grid"_s));
    QVERIFY(!ui.view(u"list"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"grid"_s)), top);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void UiTests::uiColumnsBrowse()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_3, Qt::ControlModifier);
    QVERIFY(ui.view(u"columns"_s));

    // The columns are made again as they come and go, so each is looked up anew.
    const auto column = [&ui](int n) { return ui.view(u"column"_s + QString::number(n)); };

    QTRY_COMPARE(UiHarness::shownNames(column(1)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(column(1)), u"Alpha"_s);
    QVERIFY(!column(2));

    ui.press(Qt::Key_Right);
    QTRY_COMPARE(UiHarness::shownNames(column(2)), (QStringList{u"Beta"_s, u"d.txt"_s}));
    QCOMPARE(ui.browser().activeColumn(), 0);
    QCOMPARE(UiHarness::currentName(column(1)), u"Alpha"_s);

    ui.press(Qt::Key_Right);
    QCOMPARE(ui.browser().activeColumn(), 1);
    QTRY_COMPARE(UiHarness::currentName(column(2)), u"Beta"_s);
    QTRY_COMPARE(UiHarness::shownNames(column(3)), (QStringList{u"c.txt"_s}));
    QCOMPARE(ui.browser().folder(), ui.root());

    ui.press(Qt::Key_Left);
    QTRY_VERIFY(!column(3));
    QCOMPARE(ui.browser().activeColumn(), 0);
    QCOMPARE(UiHarness::currentName(column(1)), u"Alpha"_s);
    QCOMPARE(UiHarness::shownNames(column(2)), (QStringList{u"Beta"_s, u"d.txt"_s}));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void UiTests::uiGridMovesByRows()
{
    UiHarness ui;
    QStringList names;
    for (int i = 1; i <= 12; ++i) {
        names.append(u"f%1.txt"_s.arg(i, 2, 10, QLatin1Char('0')));
        QVERIFY(ui.makeFile(names.last()));
    }
    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QQuickItem *grid = ui.view(u"grid"_s);
    QVERIFY(grid);

    // A window where four and a half tiles' width is free: four fit in a row.
    const qreal cell = grid->property("cellWidth").toReal();
    QVERIFY(cell > 0);
    QQuickWindow *w = ui.window();
    w->setWidth(int(w->width() - grid->width() + cell * 4.5));
    QTRY_COMPARE(int(grid->width() / cell), 4);

    QTRY_COMPARE(UiHarness::shownNames(grid), names);
    QTRY_COMPARE(UiHarness::currentName(grid), u"f01.txt"_s);

    ui.press(Qt::Key_Right);
    QCOMPARE(UiHarness::currentName(grid), u"f02.txt"_s);
    const QQuickItem *second = UiHarness::delegateAt(grid, 1);
    QVERIFY(second);
    QCOMPARE(second->y(), UiHarness::delegateAt(grid, 0)->y());

    ui.press(Qt::Key_Down);
    QCOMPARE(UiHarness::currentName(grid), u"f06.txt"_s);
    // The tile right below, on the next row.
    const QQuickItem *below = UiHarness::delegateAt(grid, 5);
    QVERIFY(below);
    QCOMPARE(below->x(), second->x());
    QVERIFY(below->y() > second->y());

    ui.press(Qt::Key_End);
    QCOMPARE(UiHarness::currentName(grid), u"f12.txt"_s);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void UiTests::uiStaysResponsiveOnALargeFolder()
{
    UiHarness ui;
    constexpr int files = 20000;
    const QString big = QDir(ui.root()).filePath(u"big"_s);
    QVERIFY(QDir().mkpath(big));
    for (int i = 0; i < files; ++i) {
        QFile file(big + u"/n%1"_s.arg(i, 5, 10, QLatin1Char('0')));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"big"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"big"_s);

    // Frames are asked for all the time; when the window stops drawing, the gap shows.
    QQuickWindow *w = ui.window();
    QElapsedTimer clock;
    QList<qint64> frames;
    const QMetaObject::Connection counting = QObject::connect(
        w, &QQuickWindow::frameSwapped, w, [&] { frames.append(clock.elapsed()); }, Qt::DirectConnection);
    QTimer ask;
    QObject::connect(&ask, &QTimer::timeout, w, &QQuickWindow::update);
    ask.start(10);

    clock.start();
    frames.append(0);
    ui.press(Qt::Key_Return);
    const auto firstName = [list] {
        const QQuickItem *row = UiHarness::delegateAt(list, 0);
        return row ? row->property("name").toString() : QString();
    };
    QTRY_COMPARE_WITH_TIMEOUT(firstName(), u"n00000"_s, 1000);
    const qint64 firstRows = clock.elapsed();
    QVERIFY2(firstRows < 1000, qPrintable(u"first rows after %1 ms"_s.arg(firstRows)));

    QTRY_COMPARE_WITH_TIMEOUT(ui.browser().model()->count(), files, 10000);
    QTRY_VERIFY(!ui.browser().model()->isLoading());
    // Moving to the end of all of them must not stall the window either.
    ui.press(Qt::Key_End);
    QTRY_COMPARE(UiHarness::currentName(list), u"n19999"_s);
    QTest::qWait(300);
    ask.stop();
    QObject::disconnect(counting);
    frames.append(clock.elapsed()); // a stall right at the end counts too

    qint64 gap = 0;
    for (qsizetype i = 1; i < frames.size(); ++i)
        gap = std::max(gap, frames.at(i) - frames.at(i - 1));
    qInfo("first rows after %lld ms, %lld frames, longest gap %lld ms", firstRows, qint64(frames.size()), gap);
    QVERIFY2(frames.size() > 20, qPrintable(u"only %1 frames"_s.arg(frames.size())));
    QVERIFY2(gap <= 250, qPrintable(u"%1 ms without a frame"_s.arg(gap)));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
