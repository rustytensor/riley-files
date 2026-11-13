// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The Preferences panel and the pane widths in the main window, driven with the mouse and the
// keys through the real window on real files. Expected results are the ones written down in
// the milestone's test list.

#include "browser.h"
#include "uiharness.h"

#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStyleHints>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void collect(QQuickItem *at, const QString &name, QList<QQuickItem *> &into)
{
    if (!at)
        return;
    if (at->objectName() == name && at->isVisible())
        into.append(at);
    const auto children = at->childItems();
    for (QQuickItem *child : children)
        collect(child, name, into);
}

// The texts of the items with this objectName, top to bottom.
QStringList textsOf(UiHarness &ui, const QString &name)
{
    QList<QQuickItem *> items;
    collect(ui.window()->contentItem(), name, items);
    std::sort(items.begin(), items.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->mapToScene(QPointF()).y() < b->mapToScene(QPointF()).y();
    });
    QStringList texts;
    for (QQuickItem *item : items)
        texts.append(item->property("text").toString());
    return texts;
}

QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

void click(UiHarness &ui, QQuickItem *item)
{
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier, centreOf(item));
}

// Waits out the double-click time, so the next click is a click of its own.
void pauseClicks()
{
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 150);
}

// Drags an item sideways by `dx`, in steps, as a hand does.
void drag(UiHarness &ui, QQuickItem *item, int dx)
{
    const QPoint from = centreOf(item);
    QTest::mousePress(ui.window(), Qt::LeftButton, Qt::NoModifier, from);
    const int steps = 10;
    for (int i = 1; i <= steps; ++i)
        QTest::mouseMove(ui.window(), from + QPoint(dx * i / steps, 0));
    QTest::mouseRelease(ui.window(), Qt::LeftButton, Qt::NoModifier, from + QPoint(dx, 0));
}

int widthOf(UiHarness &ui, const QString &name)
{
    QQuickItem *item = ui.view(name);
    return item ? int(item->width()) : -1;
}

// The first column of the columns view, its divider included.
int columnWidth(UiHarness &ui)
{
    QQuickItem *list = ui.view(u"column1"_s);
    return list && list->parentItem() ? int(list->parentItem()->width()) : -1;
}

void choosePaneWidths(UiHarness &ui, const QString &label)
{
    ui.press(Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"preferences"_s));
    QQuickItem *choice = ui.view(u"prefChoice_paneWidths_"_s + label);
    QVERIFY(choice);
    click(ui, choice);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"preferences"_s));
}

} // namespace

class PrefsUiTests : public QObject
{
    Q_OBJECT

private slots:
    void optionsPanelSavesAndApplies();
    void paneWidthsFixedAndProportional();
    void paneWidthsDragAndReset();
};

void PrefsUiTests::optionsPanelSavesAndApplies()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));

    ui.press(Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"preferences"_s));
    QCOMPARE(textsOf(ui, u"prefSection"_s), (QStringList{u"General"_s, u"Appearance"_s, u"Behaviour"_s}));

    // Default view: Columns, kept in the program's settings file at once.
    QQuickItem *columns = ui.view(u"prefChoice_defaultView_Columns"_s);
    QVERIFY(columns);
    click(ui, columns);
    const QString conf = ui.scratch() + u"/config/riley-files/riley-files.conf"_s;
    QTRY_COMPARE(QSettings(conf, QSettings::IniFormat).value(u"View/default"_s).toString(), u"columns"_s);
    QFile file(conf);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    QVERIFY2(text.contains("[View]") && text.contains("default=columns"), text.constData());
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"preferences"_s));

    // The view used last is the list; a new window still starts in the default, columns.
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"list"_s));
    ui.stop();
    QVERIFY(ui.start({ui.root()}));
    QTRY_VERIFY(ui.view(u"columns"_s));

    // One click on a folder only puts the cursor on it, until "open with a single click" is on.
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QQuickItem *list = ui.view(u"list"_s);
    QTRY_VERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QQuickItem *alpha = UiHarness::delegateAt(list, 0);
    QVERIFY(alpha);
    click(ui, alpha);
    pauseClicks();
    QCOMPARE(ui.browser().folder(), ui.root());
    QCOMPARE(UiHarness::currentName(list), u"Alpha"_s);

    ui.press(Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"preferences"_s));
    QQuickItem *single = ui.view(u"prefSwitch_singleClick"_s);
    QVERIFY(single);
    click(ui, single);
    QTRY_COMPARE(QSettings(conf, QSettings::IniFormat).value(u"general/singleClick"_s).toString(), u"true"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"preferences"_s));
    pauseClicks();

    QQuickItem *zeta = UiHarness::delegateAt(list, 1);
    QVERIFY(zeta);
    click(ui, zeta);
    QTRY_COMPARE(ui.browser().folder(), ui.root() + u"/Zeta"_s);

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void PrefsUiTests::paneWidthsFixedAndProportional()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_I, Qt::ControlModifier); // the details pane
    ui.press(Qt::Key_3, Qt::ControlModifier); // the columns view
    QTRY_VERIFY(ui.view(u"details"_s));
    QTRY_VERIFY(ui.view(u"column1"_s));

    // Fixed, the default.
    QCOMPARE(widthOf(ui, u"sidebar"_s), 200);
    QCOMPARE(widthOf(ui, u"details"_s), 300);
    QCOMPARE(columnWidth(ui), 260);

    // Proportional: the fixed width x window width / 1280, rounded, then limited.
    choosePaneWidths(ui, u"Proportional"_s);
    ui.window()->setWidth(2560);
    QTRY_COMPARE(int(ui.window()->width()), 2560);
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 400);
    QCOMPARE(widthOf(ui, u"details"_s), 600);
    QCOMPARE(columnWidth(ui), 480); // 520, limited to 480

    ui.window()->setWidth(960);
    QTRY_COMPARE(int(ui.window()->width()), 960);
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 150);
    QCOMPARE(widthOf(ui, u"details"_s), 225);
    QCOMPARE(columnWidth(ui), 195);

    // The lower limits: at 480, 75 -> 140, 112.5 -> 220, 97.5 -> 180.
    ui.window()->setWidth(480);
    QTRY_COMPARE(int(ui.window()->width()), 480);
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 140);
    QCOMPARE(widthOf(ui, u"details"_s), 220);
    QCOMPARE(columnWidth(ui), 180);

    // Back to fixed, whatever the window's width.
    choosePaneWidths(ui, u"Fixed"_s);
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 200);
    QCOMPARE(widthOf(ui, u"details"_s), 300);
    QCOMPARE(columnWidth(ui), 260);

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void PrefsUiTests::paneWidthsDragAndReset()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 200);

    QQuickItem *divider = ui.view(u"sidebarDivider"_s);
    QVERIFY(divider);
    drag(ui, divider, 50);
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 250);

    // Remembered by a new window.
    ui.stop();
    QVERIFY(ui.start({ui.root()}));
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 250);

    // A double-click on the divider: the fixed width again.
    divider = ui.view(u"sidebarDivider"_s);
    QVERIFY(divider);
    QTest::mouseDClick(ui.window(), Qt::LeftButton, Qt::NoModifier, centreOf(divider));
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 200);
    pauseClicks();

    // Dragged past the limit, it stops at 400.
    drag(ui, divider, 300);
    QTRY_COMPARE(widthOf(ui, u"sidebar"_s), 400);

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

QTEST_MAIN(PrefsUiTests)
#include "prefsui_tests.moc"
