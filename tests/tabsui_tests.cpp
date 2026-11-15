// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Tabs in the real window: opened, switched between and closed with the keys; each keeps its own
// folder, history, view, cursor and hidden files, and goes on following its folder while it is
// in the background; a folder opens in a new tab from the middle button or Ctrl+Return, and the
// command line's locations open as tabs. Expected results are the ones written down in the
// milestone's test list.

#include "browser.h"
#include "preferences.h"
#include "tabs.h"
#include "uiharness.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// HOME for one test, given back after it.
class HomeFor
{
public:
    explicit HomeFor(const QString &home) : m_before(qgetenv("HOME")) { qputenv("HOME", QFile::encodeName(home)); }
    ~HomeFor() { qputenv("HOME", m_before); }
    HomeFor(const HomeFor &) = delete;
    HomeFor &operator=(const HomeFor &) = delete;

private:
    QByteArray m_before;
};

// What the tab bar's labels say, left to right.
QStringList shownTitles(const UiHarness &ui)
{
    QStringList titles;
    for (int row = 0;; ++row) {
        QQuickItem *tab = ui.view(u"tab"_s + QString::number(row));
        if (!tab)
            break;
        titles.append(tab->property("title").toString());
    }
    return titles;
}

QStringList sorted(QStringList names)
{
    std::sort(names.begin(), names.end());
    return names;
}

constexpr Qt::KeyboardModifiers ctrl = Qt::ControlModifier;
constexpr Qt::KeyboardModifiers ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

} // namespace

class TabsUiTests : public QObject
{
    Q_OBJECT

private slots:
    void uiTabsOpenAndSwitch();
    void uiTabsKeepTheirOwnState();
    void uiTabsOpenFromAFolder();
    void uiTabsSharePreferences();
};

void TabsUiTests::uiTabsOpenAndSwitch()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    const QString home = QDir(ui.scratch()).filePath(u"home"_s);
    QVERIFY(QDir().mkpath(home));
    HomeFor homeFor(home);
    QVERIFY(ui.start({ui.root()}));
    const QString root = ui.root();
    const QString zeta = root + u"/Zeta"_s;
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));

    // One tab: no tab bar.
    QCOMPARE(ui.tabs().count(), 1);
    QVERIFY(ui.item(u"tabBar"_s));
    QVERIFY(!ui.view(u"tabBar"_s));

    // Ctrl+T: a second tab, active, on the same folder; the bar shows.
    ui.press(Qt::Key_T, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 2);
    QCOMPARE(ui.tabs().current(), 1);
    QCOMPARE(ui.browser().folder(), root);
    QTRY_VERIFY(ui.view(u"tabBar"_s));

    // In tab 2, Zeta.
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"Alpha"_s);
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"Zeta"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), zeta);

    // Ctrl+Tab and Ctrl+Shift+Tab (the keyboard sends Backtab), going round.
    ui.press(Qt::Key_Tab, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    ui.press(Qt::Key_Backtab, ctrlShift);
    QTRY_COMPARE(ui.tabs().current(), 1);
    QCOMPARE(ui.browser().folder(), zeta);
    ui.press(Qt::Key_Tab, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    ui.press(Qt::Key_Backtab, ctrlShift);
    QTRY_COMPARE(ui.tabs().current(), 1);

    // Ctrl+PgUp and Ctrl+PgDn the same.
    ui.press(Qt::Key_PageUp, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    ui.press(Qt::Key_PageUp, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 1);
    QCOMPARE(ui.browser().folder(), zeta);
    ui.press(Qt::Key_PageDown, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    ui.press(Qt::Key_PageDown, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 1);

    // Alt+1 and Alt+2.
    ui.press(Qt::Key_1, Qt::AltModifier);
    QTRY_COMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    ui.press(Qt::Key_2, Qt::AltModifier);
    QTRY_COMPARE(ui.tabs().current(), 1);
    QCOMPARE(ui.browser().folder(), zeta);

    // The titles: the folders' names, and Home for the home folder.
    QTRY_COMPARE(shownTitles(ui), (QStringList{QFileInfo(root).fileName(), u"Zeta"_s}));
    ui.press(Qt::Key_Home, Qt::AltModifier);
    QTRY_COMPARE(ui.browser().folder(), home);
    QTRY_COMPARE(shownTitles(ui), (QStringList{QFileInfo(root).fileName(), u"Home"_s}));
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(ui.browser().folder(), zeta);
    QTRY_COMPARE(shownTitles(ui), (QStringList{QFileInfo(root).fileName(), u"Zeta"_s}));

    // Ctrl+W closes the tab; the bar hides with one left.
    ui.press(Qt::Key_W, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 1);
    QCOMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    QTRY_VERIFY(!ui.view(u"tabBar"_s));
    QVERIFY(ui.window()->isVisible());

    // Ctrl+Shift+T: the closed tab again, on Zeta, with its history.
    ui.press(Qt::Key_T, ctrlShift);
    QTRY_COMPARE(ui.tabs().count(), 2);
    QCOMPARE(ui.tabs().current(), 1);
    QCOMPARE(ui.browser().folder(), zeta);
    QTRY_VERIFY(ui.view(u"tabBar"_s));
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(ui.browser().folder(), root);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));

    // Ctrl+W on the only tab closes the window.
    ui.press(Qt::Key_W, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 1);
    QVERIFY(ui.window()->isVisible());
    ui.press(Qt::Key_W, ctrl);
    QTRY_VERIFY(!ui.window()->isVisible());
}

void TabsUiTests::uiTabsKeepTheirOwnState()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.makeFile(u"Alpha/.secret"_s));
    QVERIFY(ui.start({ui.root()}));
    const QString root = ui.root();
    const QString alpha = root + u"/Alpha"_s;
    const QString zeta = root + u"/Zeta"_s;
    const QStringList rootNames{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s};
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), rootNames);

    // Tab 1: the list, been to Zeta and back, the cursor on e.txt.
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"Alpha"_s);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), zeta);
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(ui.browser().folder(), root);
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"Zeta"_s);
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"e.txt"_s);
    QCOMPARE(ui.browser().view(), Browser::List);
    QVERIFY(!ui.browser().showHidden());

    // Tab 2: the grid on Alpha, the cursor on d.txt, hidden files shown.
    ui.press(Qt::Key_T, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 2);
    ui.press(Qt::Key_1, ctrl);
    QTRY_VERIFY(ui.view(u"grid"_s));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"grid"_s)), u"Alpha"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), alpha);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"grid"_s)), (QStringList{u"Beta"_s, u"d.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"grid"_s)), u"Beta"_s);
    ui.press(Qt::Key_Right);
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"grid"_s)), u"d.txt"_s);
    ui.press(Qt::Key_H, ctrl);
    QTRY_COMPARE(sorted(UiHarness::shownNames(ui.view(u"grid"_s))),
                 (QStringList{u".secret"_s, u"Beta"_s, u"d.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"grid"_s)), u"d.txt"_s);

    // Back and forth: each tab as it was left.
    for (int round = 0; round < 2; ++round) {
        ui.press(Qt::Key_Tab, ctrl);
        QTRY_COMPARE(ui.tabs().current(), 0);
        QCOMPARE(ui.browser().folder(), root);
        QCOMPARE(ui.browser().view(), Browser::List);
        QTRY_VERIFY(ui.view(u"list"_s));
        QVERIFY(!ui.view(u"grid"_s));
        QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), rootNames);
        QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"e.txt"_s);
        QVERIFY(!ui.browser().showHidden());
        QVERIFY(!ui.browser().canGoBack());
        QVERIFY(ui.browser().canGoForward());

        ui.press(Qt::Key_Tab, ctrl);
        QTRY_COMPARE(ui.tabs().current(), 1);
        QCOMPARE(ui.browser().folder(), alpha);
        QCOMPARE(ui.browser().view(), Browser::Grid);
        QTRY_VERIFY(ui.view(u"grid"_s));
        QVERIFY(!ui.view(u"list"_s));
        QTRY_COMPARE(sorted(UiHarness::shownNames(ui.view(u"grid"_s))),
                     (QStringList{u".secret"_s, u"Beta"_s, u"d.txt"_s}));
        QTRY_COMPARE(UiHarness::currentName(ui.view(u"grid"_s)), u"d.txt"_s);
        QVERIFY(ui.browser().showHidden());
        QVERIFY(ui.browser().canGoBack());
        QVERIFY(!ui.browser().canGoForward());
    }

    // Tab 1 forward to Zeta; a file made there while tab 2 is in front shows when tab 1 is back.
    ui.press(Qt::Key_Tab, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    ui.press(Qt::Key_Right, Qt::AltModifier);
    QTRY_COMPARE(ui.browser().folder(), zeta);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"z.txt"_s}));
    ui.press(Qt::Key_Tab, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 1);
    QVERIFY(ui.makeFile(u"Zeta/y.txt"_s));
    ui.press(Qt::Key_Tab, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"y.txt"_s, u"z.txt"_s}),
                              3000);

    // Tab 2's history: back to the folder it came from.
    ui.press(Qt::Key_Tab, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 1);
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(ui.browser().folder(), root);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void TabsUiTests::uiTabsOpenFromAFolder()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    const QString root = ui.root();
    const QString alpha = root + u"/Alpha"_s;
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"Alpha"_s);

    // The middle button on Alpha: a new tab on it, in the background.
    QQuickItem *row = UiHarness::delegateAt(list, 0);
    QVERIFY(row);
    const QPointF at = row->mapToScene(QPointF(row->width() / 2, row->height() / 2));
    QTest::mouseClick(ui.window(), Qt::MiddleButton, Qt::NoModifier, at.toPoint());
    QTRY_COMPARE(ui.tabs().count(), 2);
    QCOMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    QTRY_COMPARE(ui.tabs().at(1)->folder(), alpha);
    QTRY_VERIFY(ui.view(u"tabBar"_s));
    QTRY_COMPARE(shownTitles(ui), (QStringList{QFileInfo(root).fileName(), u"Alpha"_s}));

    // Ctrl+Return with the cursor on Alpha: another, next to the current tab, in the background.
    QCOMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"Alpha"_s);
    ui.press(Qt::Key_Return, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 3);
    QCOMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    QTRY_COMPARE(ui.tabs().at(1)->folder(), alpha);
    QCOMPARE(ui.tabs().at(2)->folder(), alpha);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));

    // Two folders on the command line: two tabs, the first active.
    QVERIFY(ui.start({root, root + u"/Zeta"_s}));
    QTRY_COMPARE(ui.tabs().count(), 2);
    QCOMPARE(ui.tabs().current(), 0);
    QCOMPARE(ui.browser().folder(), root);
    QTRY_COMPARE(ui.tabs().at(1)->folder(), root + u"/Zeta"_s);
    QTRY_VERIFY(ui.view(u"tabBar"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

// Marc's tabs are one window: an option set in the Preferences of one tab holds in every tab, open or closed, at once.
// Fixture root: Alpha/, Zeta/, e.txt. Folders first -> [Alpha, Zeta, e.txt]; off -> by name [Alpha, e.txt, Zeta].
void TabsUiTests::uiTabsSharePreferences()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    const QString home = QDir(ui.scratch()).filePath(u"home"_s);
    QVERIFY(QDir().mkpath(home));
    HomeFor homeFor(home);
    QVERIFY(ui.start({ui.root()}));
    const QStringList foldersFirst{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s};
    const QStringList byName{u"Alpha"_s, u"e.txt"_s, u"Zeta"_s};
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), foldersFirst);

    ui.press(Qt::Key_T, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 2);
    QCOMPARE(ui.tabs().current(), 1);
    // What the Preferences panel does when the option is turned off in the active tab.
    ui.tabs().at(1)->prefs()->setValue(u"foldersFirst"_s, u"false"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), byName);
    // The other tab follows at once.
    QCOMPARE(ui.tabs().at(0)->prefs()->value(u"foldersFirst"_s), u"false"_s);
    ui.press(Qt::Key_PageUp, ctrl);
    QTRY_COMPARE(ui.tabs().current(), 0);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), byName);

    // A closed tab follows too, and comes back with the option as it is now.
    ui.press(Qt::Key_W, ctrl);
    QTRY_COMPARE(ui.tabs().count(), 1);
    ui.tabs().at(0)->prefs()->setValue(u"foldersFirst"_s, u"true"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), foldersFirst);
    ui.press(Qt::Key_T, ctrl | Qt::ShiftModifier);
    QTRY_COMPARE(ui.tabs().count(), 2);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), foldersFirst);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(TabsUiTests)
#include "tabsui_tests.moc"
