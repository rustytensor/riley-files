// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The menu bar, the context menus and the list of keyboard shortcuts in the main window,
// driven with the mouse and the keys through the real window on real files. Expected results
// are the ones written down in the milestone's test list.

#include "actions.h"
#include "browser.h"
#include "uiharness.h"

#include <QKeySequence>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void collect(QQuickItem *at, const QString &name, QList<QQuickItem *> &into)
{
    if (!at)
        return;
    if (at->objectName() == name)
        into.append(at);
    const auto children = at->childItems();
    for (QQuickItem *child : children)
        collect(child, name, into);
}

// Every item with this objectName below `root`, in the order of `property` (row, index or x).
QList<QQuickItem *> itemsNamed(QQuickItem *root, const QString &name, const char *property)
{
    QList<QQuickItem *> items;
    collect(root, name, items);
    std::sort(items.begin(), items.end(), [property](QQuickItem *a, QQuickItem *b) {
        return a->property(property).toReal() < b->property(property).toReal();
    });
    return items;
}

QQuickItem *level(UiHarness &ui, int index)
{
    return ui.view(u"menuLevel%1"_s.arg(index));
}

// The rows of an open menu level, without its separators: "label" or "label|shortcut".
QStringList menuRows(UiHarness &ui, int index, bool withShortcuts)
{
    QStringList rows;
    const auto items = itemsNamed(level(ui, index), u"menuItem"_s, "row");
    for (QQuickItem *item : items) {
        if (item->property("separator").toBool())
            continue;
        const QString label = item->property("label").toString();
        rows.append(withShortcuts ? label + u'|' + item->property("shortcut").toString() : label);
    }
    return rows;
}

QQuickItem *menuRow(UiHarness &ui, int index, const QString &label)
{
    const auto items = itemsNamed(level(ui, index), u"menuItem"_s, "row");
    for (QQuickItem *item : items) {
        if (item->property("label").toString() == label)
            return item;
    }
    return nullptr;
}

QString currentRow(UiHarness &ui, int index)
{
    const auto items = itemsNamed(level(ui, index), u"menuItem"_s, "row");
    for (QQuickItem *item : items) {
        if (item->property("current").toBool())
            return item->property("label").toString();
    }
    return {};
}

void click(UiHarness &ui, QQuickItem *item, Qt::MouseButton button = Qt::LeftButton, QPointF at = {-1, -1})
{
    QVERIFY(item);
    const QPointF local = at.x() < 0 ? QPointF(item->width() / 2, item->height() / 2) : at;
    QTest::mouseClick(ui.window(), button, Qt::NoModifier, item->mapToScene(local).toPoint());
}

QQuickItem *menuTitle(UiHarness &ui, const QString &title)
{
    const auto titles = itemsNamed(ui.item(u"menuBar"_s), u"menuTitle"_s, "x");
    for (QQuickItem *item : titles) {
        if (item->property("title").toString() == title)
            return item;
    }
    return nullptr;
}

// The rows of `rows` whose label is one of the expected ones, so the expected order is checked
// and a menu may hold more.
QStringList keepExpected(const QStringList &rows, const QStringList &expected)
{
    QStringList labels;
    for (const QString &row : expected)
        labels.append(row.section(u'|', 0, 0));
    QStringList kept;
    for (const QString &row : rows) {
        if (labels.contains(row.section(u'|', 0, 0)))
            kept.append(row);
    }
    return kept;
}

} // namespace

class MenuUiTests : public QObject
{
    Q_OBJECT

private slots:
    void menuBarHasEveryAction();
    void uiContextMenu();
};

void MenuUiTests::menuBarHasEveryAction()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));

    // The menus, left to right.
    QStringList titles;
    for (QQuickItem *item : itemsNamed(ui.item(u"menuBar"_s), u"menuTitle"_s, "x"))
        titles.append(item->property("title").toString());
    QCOMPARE(titles, (QStringList{u"File"_s, u"Edit"_s, u"View"_s, u"Go"_s, u"Help"_s}));

    const QList<QStringList> expected = {
        {u"New Window|Ctrl+N"_s, u"New Folder|Ctrl+Shift+N"_s, u"Open|Return"_s, u"Open With|"_s, u"Rename|F2"_s,
         u"Move to Trash|Delete"_s, u"Close Window|Ctrl+W"_s, u"Quit|Ctrl+Q"_s},
        {u"Undo|Ctrl+Z"_s, u"Redo|Ctrl+Shift+Z"_s, u"Cut|Ctrl+X"_s, u"Copy|Ctrl+C"_s, u"Paste|Ctrl+V"_s,
         u"Select All|Ctrl+A"_s, u"Preferences|Ctrl+,"_s},
        {u"Grid|Ctrl+1"_s, u"List|Ctrl+2"_s, u"Columns|Ctrl+3"_s, u"Tree|T"_s, u"Hidden Files|Ctrl+H"_s,
         u"Sidebar|F9"_s, u"Details|Ctrl+I"_s, u"Sort By|"_s, u"Pane Widths|"_s},
        {u"Back|Alt+Left"_s, u"Forward|Alt+Right"_s, u"Up|Alt+Up"_s, u"Home|Alt+Home"_s, u"Location|Ctrl+L"_s},
        {u"Keyboard Shortcuts|F1"_s, u"About|"_s},
    };

    // File opens from its title; Right goes on to each next menu.
    click(ui, menuTitle(ui, u"File"_s));
    QList<QStringList> shown;
    for (int menu = 0; menu < titles.size(); ++menu) {
        QTRY_VERIFY(level(ui, 0));
        QCOMPARE(ui.browser().actions()->barMenu(), menu);
        shown.append(menuRows(ui, 0, true));
        QCOMPARE(keepExpected(shown.last(), expected.at(menu)), expected.at(menu));
        ui.press(Qt::Key_Right);
    }
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));

    // The two submenus of View.
    click(ui, menuTitle(ui, u"View"_s));
    QTRY_VERIFY(level(ui, 0));
    click(ui, menuRow(ui, 0, u"Sort By"_s));
    QTRY_VERIFY(level(ui, 1));
    QCOMPARE(menuRows(ui, 1, false), (QStringList{u"Name"_s, u"Size"_s, u"Modified"_s, u"Kind"_s}));
    click(ui, menuRow(ui, 0, u"Pane Widths"_s));
    QTRY_COMPARE(menuRows(ui, 1, false), (QStringList{u"Fixed"_s, u"Automatic"_s, u"Proportional"_s}));
    ui.press(Qt::Key_Escape);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));

    // Every action with a key, as the list of shortcuts (F1) shows them all, is in exactly one
    // menu of the bar, with the same key.
    ui.press(Qt::Key_F1);
    QQuickItem *overlay = ui.view(u"shortcuts"_s);
    QVERIFY(overlay);
    const auto rows = itemsNamed(overlay, u"shortcutRow"_s, "index");
    QVERIFY(rows.size() >= 30);
    QStringList listed;
    for (QQuickItem *row : rows) {
        const QString entry = row->property("label").toString() + u'|' + row->property("shortcut").toString();
        listed.append(entry);
        int in = 0;
        for (const QStringList &menu : shown)
            in += int(menu.count(entry));
        QVERIFY2(in == 1, qPrintable(u"%1 is in %2 menus"_s.arg(entry).arg(in)));
    }
    for (const QStringList &menu : expected) {
        for (const QString &entry : menu) {
            if (!entry.endsWith(u'|'))
                QVERIFY2(listed.contains(entry), qPrintable(entry + u" is not in the shortcuts list"_s));
        }
    }
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"shortcuts"_s));

    // Straight from the registry, one action at a time: every action that has a key is in
    // exactly one menu of the bar, with that key.
    Actions *actions = ui.browser().actions();
    int keyed = 0;
    for (const QString &id : actions->ids()) {
        const QString key = actions->shortcut(id);
        if (key.isEmpty())
            continue;
        ++keyed;
        const QString entry = actions->label(id) + u'|' + key;
        int in = 0;
        for (const QStringList &menu : shown)
            in += int(menu.count(entry));
        QVERIFY2(in == 1, qPrintable(u"action %1 (%2) is in %3 menus"_s.arg(id, entry).arg(in)));
    }
    QVERIFY(keyed >= 30);

    // Every key in the registry's key list (each action's own key and the others that do the
    // same) leads back to a registered action with that row's name.
    int keys = 0;
    for (const QVariant &value : actions->shortcutList()) {
        const QVariantMap row = value.toMap();
        QStringList texts{row.value(u"shortcut"_s).toString()};
        const QString others = row.value(u"others"_s).toString();
        if (!others.isEmpty())
            texts += others.split(u", "_s);
        for (const QString &text : texts) {
            const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
            QVERIFY2(sequence.count() == 1, qPrintable(text + u" is not one key"_s));
            const QKeyCombination combination = sequence[0];
            const QString id = actions->idForKey(int(combination.key()), combination.keyboardModifiers());
            QVERIFY2(!id.isEmpty() && actions->has(id),
                     qPrintable(text + u" belongs to no registered action"_s));
            QVERIFY2(actions->label(id) == row.value(u"label"_s).toString(),
                     qPrintable(u"%1 is listed for %2 but belongs to %3"_s.arg(text, row.value(u"label"_s).toString(),
                                                                              actions->label(id))));
            ++keys;
        }
    }
    QVERIFY(keys >= keyed);

    // Choosing View > Grid shows the grid.
    click(ui, menuTitle(ui, u"View"_s));
    QTRY_VERIFY(level(ui, 0));
    click(ui, menuRow(ui, 0, u"Grid"_s));
    QTRY_VERIFY(ui.view(u"grid"_s));
    QVERIFY(!level(ui, 0));

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void MenuUiTests::uiContextMenu()
{
    UiHarness ui;
    QVERIFY(ui.makeFile(u"a.txt"_s));
    QVERIFY(ui.makeFile(u"b.txt"_s));
    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);

    const QStringList onFile = {u"Open"_s,          u"Open With"_s,   u"Cut"_s,        u"Copy"_s,
                                u"Rename"_s,        u"Move to Trash"_s, u"Change Icon"_s, u"Properties"_s};
    const QStringList onEmpty = {u"New Folder"_s, u"Paste"_s, u"Hidden Files"_s, u"Sort By"_s};

    // The Menu key on a file, and Shift+F10 the same.
    ui.press(Qt::Key_Menu);
    QTRY_VERIFY(level(ui, 0));
    QCOMPARE(menuRows(ui, 0, false), onFile);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));
    ui.press(Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY(level(ui, 0));
    QCOMPARE(menuRows(ui, 0, false), onFile);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));

    // A right click on the empty space below the rows.
    click(ui, list, Qt::RightButton, QPointF(list->width() / 2, list->height() - 10));
    QTRY_VERIFY(level(ui, 0));
    QCOMPARE(menuRows(ui, 0, false), onEmpty);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));

    // A right click on b.txt: the cursor goes there and the menu is the file's.
    QQuickItem *second = UiHarness::delegateAt(list, 1);
    QVERIFY(second);
    click(ui, second, Qt::RightButton);
    QTRY_VERIFY(level(ui, 0));
    QCOMPARE(menuRows(ui, 0, false), onFile);
    QCOMPARE(UiHarness::currentName(list), u"b.txt"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));

    // Choosing Rename, by keys, starts the name editor on the file.
    ui.press(Qt::Key_Menu);
    QTRY_VERIFY(level(ui, 0));
    QCOMPARE(currentRow(ui, 0), u"Open"_s);
    for (int i = 0; i < 10 && currentRow(ui, 0) != u"Rename"_s; ++i)
        ui.press(Qt::Key_Down);
    QCOMPARE(currentRow(ui, 0), u"Rename"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(!level(ui, 0));
    QTRY_VERIFY(ui.browser().ops()->editing());
    QCOMPARE(ui.browser().ops()->editingPath(), ui.root() + u"/b.txt"_s);
    QQuickItem *field = ui.view(u"renameField"_s);
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.browser().ops()->editing());

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

QTEST_MAIN(MenuUiTests)
#include "menuui_tests.moc"
