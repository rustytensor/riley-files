// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Default applications (mimeapps.list, Open With, Return opening a file with its application)
// and custom icons (Change Icon, kept across a new window and a rename, Reset Icon), driven
// through the real window on real files. Expected results are the ones written down in the
// milestone's test list.

#include "browser.h"
#include "uiharness.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
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

QQuickItem *firstNamed(QQuickItem *root, const QString &name)
{
    QList<QQuickItem *> items;
    collect(root, name, items);
    return items.isEmpty() ? nullptr : items.first();
}

// The item with this objectName that is shown (the views that are hidden keep theirs).
QQuickItem *shownNamed(UiHarness &ui, const QString &name)
{
    QList<QQuickItem *> items;
    collect(ui.window()->contentItem(), name, items);
    for (QQuickItem *item : items) {
        bool shown = item->width() > 0 && item->height() > 0;
        for (QQuickItem *at = item; at && shown; at = at->parentItem())
            shown = at->isVisible() && at->opacity() > 0;
        if (shown)
            return item;
    }
    return nullptr;
}

QQuickItem *level(UiHarness &ui, int index)
{
    return ui.view(u"menuLevel%1"_s.arg(index));
}

QList<QQuickItem *> menuItems(UiHarness &ui, int index)
{
    QList<QQuickItem *> items;
    collect(level(ui, index), u"menuItem"_s, items);
    std::sort(items.begin(), items.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->property("row").toInt() < b->property("row").toInt();
    });
    return items;
}

// The labels of an open menu level, without its separators.
QStringList menuRows(UiHarness &ui, int index)
{
    QStringList rows;
    for (QQuickItem *item : menuItems(ui, index)) {
        if (!item->property("separator").toBool())
            rows.append(item->property("label").toString());
    }
    return rows;
}

QQuickItem *menuRow(UiHarness &ui, int index, const QString &label)
{
    for (QQuickItem *item : menuItems(ui, index)) {
        if (item->property("label").toString() == label)
            return item;
    }
    return nullptr;
}

QString currentRow(UiHarness &ui, int index)
{
    for (QQuickItem *item : menuItems(ui, index)) {
        if (item->property("current").toBool())
            return item->property("label").toString();
    }
    return {};
}

void click(UiHarness &ui, QQuickItem *item)
{
    QVERIFY(item);
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

// Down through an open menu until `label` is the current row; true when it is.
bool stepTo(UiHarness &ui, int index, const QString &label)
{
    for (int i = 0; i < 20 && currentRow(ui, index) != label; ++i)
        ui.press(Qt::Key_Down);
    return currentRow(ui, index) == label;
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("(none)");
}

// The FileIcon of a view's row.
QQuickItem *iconOf(QQuickItem *view, int row)
{
    return firstNamed(UiHarness::delegateAt(view, row), u"fileIcon"_s);
}

QString chooserFirstRow(UiHarness &ui)
{
    const QVariantList rows = ui.browser().chooser()->rows();
    return rows.isEmpty() ? QString() : rows.first().toMap().value(u"label"_s).toString();
}

QJsonObject iconsJson(UiHarness &ui)
{
    QFile file(QDir(ui.scratch()).filePath(u"data/riley-files/icons.json"_s));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class AppsUiTests : public QObject
{
    Q_OBJECT

private slots:
    void defaultAppsUseMimeapps();
    void customIconsStick();
};

void AppsUiTests::defaultAppsUseMimeapps()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
    qputenv("XDG_DATA_DIRS", (scratch.filePath(u"share"_s) + u":/usr/local/share:/usr/share"_s).toUtf8());
    const auto restore = qScopeGuard([&oldDirs] {
        if (oldDirs.isNull())
            qunsetenv("XDG_DATA_DIRS");
        else
            qputenv("XDG_DATA_DIRS", oldDirs);
    });

    // Two applications for text/plain. Each is a small script that writes the path it was
    // given into its own marker file.
    for (const QString &app : {u"a"_s, u"b"_s}) {
        const QString script = scratch.filePath(u"bin/open-%1.sh"_s.arg(app));
        const QString marker = scratch.filePath(u"marker-%1"_s.arg(app));
        // The script also writes how many arguments it got, beside the marker.
        QVERIFY(writeFile(script, "#!/bin/sh\nprintf '%s' \"$1\" > '" + marker.toUtf8() + "'\nprintf '%s' \"$#\" > '"
                                      + marker.toUtf8() + ".count'\n"));
        QVERIFY(writeFile(scratch.filePath(u"share/applications/%1.desktop"_s.arg(app)),
                          "[Desktop Entry]\nType=Application\nName=App " + app.toUpper().toUtf8()
                              + "\nExec=/bin/sh \"" + script.toUtf8() + "\" %f\nMimeType=text/plain;\n"));
    }
    // The person's own choices so far, which must stay.
    const QString list = scratch.filePath(u"config/mimeapps.list"_s);
    QVERIFY(writeFile(list, "# Chosen by hand\n"
                            "[Default Applications]\n"
                            "image/png=viewer.desktop\n"
                            "\n"
                            "[Added Associations]\n"
                            "image/png=viewer.desktop;\n"));

    QVERIFY(ui.makeFile(u"a.txt"_s, 5));
    QVERIFY(ui.start({ui.root()}));
    QQuickItem *view = ui.view(u"list"_s);
    QVERIFY(view);
    QTRY_COMPARE(UiHarness::shownNames(view), (QStringList{u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(view), u"a.txt"_s);

    // Open With on a.txt lists both applications.
    ui.press(Qt::Key_Menu);
    QTRY_VERIFY(level(ui, 0));
    click(ui, menuRow(ui, 0, u"Open With"_s));
    QTRY_VERIFY(level(ui, 1));
    const QStringList apps = menuRows(ui, 1);
    QVERIFY2(apps.contains(u"App A"_s) && apps.contains(u"App B"_s), qPrintable(apps.join(u", "_s)));
    QVERIFY(apps.contains(u"Always Use for This Type"_s));
    QVERIFY(apps.contains(u"Other Application"_s));

    // Always Use for This Type > App B.
    click(ui, menuRow(ui, 1, u"Always Use for This Type"_s));
    QTRY_VERIFY(level(ui, 2));
    const QStringList always = menuRows(ui, 2);
    QVERIFY2(always.contains(u"App A"_s) && always.contains(u"App B"_s), qPrintable(always.join(u", "_s)));
    click(ui, menuRow(ui, 2, u"App B"_s));
    QTRY_VERIFY(!level(ui, 0));

    // Written under [Default Applications] in $XDG_CONFIG_HOME/mimeapps.list; every other line
    // as it was.
    QTRY_COMPARE(readFile(list), QByteArray("# Chosen by hand\n"
                                            "[Default Applications]\n"
                                            "image/png=viewer.desktop\n"
                                            "text/plain=b.desktop\n"
                                            "\n"
                                            "[Added Associations]\n"
                                            "image/png=viewer.desktop;\n"));
    QVERIFY(!QFile::exists(scratch.filePath(u"marker-a"_s)));
    QVERIFY(!QFile::exists(scratch.filePath(u"marker-b"_s)));

    // The default comes first in Open With now.
    ui.press(Qt::Key_Menu);
    QTRY_VERIFY(level(ui, 0));
    click(ui, menuRow(ui, 0, u"Open With"_s));
    QTRY_VERIFY(level(ui, 1));
    QCOMPARE(menuRows(ui, 1).value(0), u"App B"_s);
    ui.press(Qt::Key_Escape);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!level(ui, 0));

    // Return on a.txt runs B with the file's path.
    ui.press(Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(readFile(scratch.filePath(u"marker-b"_s)), (ui.root() + u"/a.txt"_s).toUtf8(), 10000);
    QVERIFY(!QFile::exists(scratch.filePath(u"marker-a"_s)));

    // A name a shell would break apart: a quote, a variable, a command separator and spaces.
    // Open With > App B gives B exactly that path as its one argument, and nothing in the name
    // is ever run or turned into other files.
    const QString awkward = u"it's $HOME; a b.txt"_s;
    const QString awkwardPath = ui.root() + u'/' + awkward;
    QVERIFY(QFile::remove(scratch.filePath(u"marker-b"_s)));
    QVERIFY(ui.makeFile(awkward, 5));
    QTRY_COMPARE(UiHarness::shownNames(view), (QStringList{u"a.txt"_s, awkward}));
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(UiHarness::currentName(view), awkward);
    ui.press(Qt::Key_Menu);
    QTRY_VERIFY(level(ui, 0));
    click(ui, menuRow(ui, 0, u"Open With"_s));
    QTRY_VERIFY(level(ui, 1));
    click(ui, menuRow(ui, 1, u"App B"_s));
    QTRY_VERIFY(!level(ui, 0));
    QTRY_COMPARE_WITH_TIMEOUT(readFile(scratch.filePath(u"marker-b"_s)), awkwardPath.toUtf8(), 10000);
    QTRY_COMPARE(readFile(scratch.filePath(u"marker-b.count"_s)), QByteArray("1"));
    QVERIFY(!QFile::exists(scratch.filePath(u"marker-a"_s)));

    // No file named "a" or "b.txt" anywhere in the test's folders, nor where applications are
    // started (the home folder) or in the folder the test runs in.
    QStringList stray;
    QDirIterator walk(scratch.path(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        if (walk.fileName() == u"a"_s || walk.fileName() == u"b.txt"_s)
            stray.append(path);
    }
    for (const QString &folder : {QDir::homePath(), QDir::currentPath()}) {
        for (const QString &name : {u"a"_s, u"b.txt"_s}) {
            if (QFileInfo::exists(QDir(folder).filePath(name)))
                stray.append(QDir(folder).filePath(name));
        }
    }
    QVERIFY2(stray.isEmpty(), qPrintable(stray.join(u", "_s)));

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void AppsUiTests::customIconsStick()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    // Two icons of the hicolor theme, in this test's own data folder (XDG_DATA_HOME).
    for (const QString &name : {u"folder-music"_s, u"folder-pictures"_s}) {
        const QString path = scratch.filePath(u"data/icons/hicolor/48x48/places/%1.png"_s.arg(name));
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QImage image(48, 48, QImage::Format_ARGB32);
        image.fill(name == u"folder-music"_s ? Qt::magenta : Qt::cyan);
        QVERIFY(image.save(path));
    }
    QVERIFY(ui.makeFile(u"D/inside.txt"_s));
    QVERIFY(ui.makeFile(u"x.txt"_s));
    const QString folder = ui.root() + u"/D"_s;

    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QQuickItem *grid = nullptr;
    QTRY_VERIFY((grid = ui.view(u"grid"_s)));
    QTRY_COMPARE(UiHarness::shownNames(grid), (QStringList{u"D"_s, u"x.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(grid), u"D"_s);
    QTRY_VERIFY(iconOf(grid, 0));
    QCOMPARE(iconOf(grid, 0)->property("category").toString(), u"folder"_s);
    QVERIFY(iconOf(grid, 0)->property("drawn").toBool());

    // Change Icon on D, from the context menu: the chooser, narrowed by typing, Return.
    ui.press(Qt::Key_Menu);
    QTRY_VERIFY(level(ui, 0));
    QVERIFY(stepTo(ui, 0, u"Change Icon"_s));
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(ui.view(u"chooser"_s));
    QTRY_VERIFY(!ui.browser().chooser()->loading());
    ui.type(u"folder-mu"_s);
    QCOMPARE(ui.browser().chooser()->query(), u"folder-mu"_s);
    QTRY_COMPARE(chooserFirstRow(ui), u"folder-music"_s);
    QTRY_VERIFY(UiHarness::delegateAt(ui.view(u"chooserList"_s), 0));
    QCOMPARE(UiHarness::delegateAt(ui.view(u"chooserList"_s), 0)->property("label").toString(), u"folder-music"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(!ui.view(u"chooser"_s));

    // Its tile uses folder-music.
    const auto showsMusic = [](QQuickItem *view, int row) {
        QQuickItem *icon = iconOf(view, row);
        if (!icon)
            return false;
        QQuickItem *image = firstNamed(icon, u"customIcon"_s);
        return icon->property("customSource").toString().endsWith(u"/folder-music"_s)
               && icon->property("showsCustom").toBool() && !icon->property("drawn").toBool() && image
               && image->isVisible() && image->property("status").toInt() == 1 /* Image.Ready */;
    };
    QTRY_VERIFY(showsMusic(grid, 0));
    QCOMPARE(iconsJson(ui).value(folder).toString(), u"folder-music"_s);
    QCOMPARE(iconOf(grid, 1)->property("customSource").toString(), QString());

    // Still after a new window.
    QVERIFY(ui.start({ui.root()}));
    if (!ui.view(u"grid"_s))
        ui.press(Qt::Key_1, Qt::ControlModifier);
    QTRY_VERIFY((grid = ui.view(u"grid"_s)));
    QTRY_COMPARE(UiHarness::shownNames(grid), (QStringList{u"D"_s, u"x.txt"_s}));
    QTRY_VERIFY(showsMusic(grid, 0));

    // Renaming D in Riley Files keeps the icon.
    QTRY_COMPARE(UiHarness::currentName(grid), u"D"_s);
    ui.press(Qt::Key_F2);
    QTRY_VERIFY(ui.browser().ops()->editing());
    QCOMPARE(ui.browser().ops()->editingPath(), folder);
    QTRY_VERIFY(shownNamed(ui, u"renameField"_s));
    QTRY_VERIFY(shownNamed(ui, u"renameField"_s)->hasActiveFocus());
    ui.type(u"Music"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(UiHarness::shownNames(grid), (QStringList{u"Music"_s, u"x.txt"_s}));
    QTRY_VERIFY(showsMusic(grid, 0));
    QCOMPARE(iconsJson(ui).value(ui.root() + u"/Music"_s).toString(), u"folder-music"_s);
    QVERIFY(!iconsJson(ui).contains(folder));

    // Reset Icon, from the File menu: the default folder icon again.
    QTRY_COMPARE(UiHarness::currentName(grid), u"Music"_s);
    ui.press(Qt::Key_F10);
    QTRY_VERIFY(level(ui, 0));
    QVERIFY(stepTo(ui, 0, u"Reset Icon"_s));
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(!level(ui, 0));
    QTRY_COMPARE(iconOf(grid, 0)->property("customSource").toString(), QString());
    QVERIFY(iconOf(grid, 0)->property("drawn").toBool());
    QCOMPARE(iconOf(grid, 0)->property("category").toString(), u"folder"_s);
    QVERIFY(iconsJson(ui).isEmpty());

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

QTEST_MAIN(AppsUiTests)
#include "appsui_tests.moc"
