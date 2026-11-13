// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Previous versions driven through the real window: Alt+V, File > Previous Versions and the
// details pane's section listing the versions with their dates; Return asking "Restore this
// version?" and restoring; Restore as Copy from a version's context menu; Escape closing the
// list; a folder's version looked through read-only and copied out of; every change inside a
// snapshot refused with its message; and nothing offered where there are no snapshots.
// Expected results are the ones written down in the milestone's test list.

#include "browser.h"
#include "snapshots.h"
#include "uiharness.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQuickTest/quicktest.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<unreadable>");
}

bool setModified(const QString &path, qint64 secs)
{
    const struct timespec times[2] = {{secs, 0}, {secs, 0}};
    return ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, AT_SYMLINK_NOFOLLOW) == 0;
}

QDateTime local(int day, int hour = 10)
{
    return QDateTime(QDate(2026, 11, day), QTime(hour, 0));
}

// The milestone's fixture in `root`: .snapshots/1, 2, 3 (taken 2026-11-01, 02, 03 at 10:00 local
// time, described "timeline") with doc.txt = "v1", "v2", "v2" (3's the same as 2's: same size
// and modified time) and D/a.txt in 1 and 2 only; the live doc.txt = "v3" and an empty live D.
bool makeSnapshots(const QString &root)
{
    const QByteArray contents[3] = {"v1", "v2", "v2"};
    const qint64 v1Time = local(1, 9).toSecsSinceEpoch();
    const qint64 v2Time = local(2, 9).toSecsSinceEpoch();
    for (int n = 1; n <= 3; ++n) {
        const QString one = root + u"/.snapshots/%1"_s.arg(n);
        const QString snap = one + u"/snapshot"_s;
        const QString utc = local(n).toUTC().toString(u"yyyy-MM-dd HH:mm:ss"_s);
        if (!writeFile(one + u"/info.xml"_s,
                       u"<?xml version=\"1.0\"?>\n<snapshot>\n  <type>single</type>\n  <num>%1</num>\n"
                       "  <date>%2</date>\n  <description>timeline</description>\n"
                       "  <cleanup>timeline</cleanup>\n</snapshot>\n"_s.arg(n).arg(utc).toUtf8()))
            return false;
        if (!writeFile(snap + u"/doc.txt"_s, contents[n - 1])
            || !setModified(snap + u"/doc.txt"_s, n == 1 ? v1Time : v2Time))
            return false;
        if (n <= 2
            && (!writeFile(snap + u"/D/a.txt"_s, "a") || !setModified(snap + u"/D/a.txt"_s, v1Time)))
            return false;
    }
    return QDir().mkpath(root + u"/D"_s) && writeFile(root + u"/doc.txt"_s, "v3");
}

QString snap(const QString &root, int n)
{
    return root + u"/.snapshots/%1/snapshot"_s.arg(n);
}

// Every path under folder with its mode, size and modified time to the nanosecond.
QStringList fingerprint(const QString &folder)
{
    QStringList paths{folder};
    QDirIterator it(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        paths.append(it.next());
    std::sort(paths.begin(), paths.end());
    QStringList lines;
    for (const QString &path : std::as_const(paths)) {
        struct stat st;
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
            lines.append(path + u" gone"_s);
            continue;
        }
        lines.append(u"%1 %2 %3 %4.%5"_s.arg(path)
                         .arg(st.st_mode, 0, 8)
                         .arg(qint64(st.st_size))
                         .arg(qint64(st.st_mtim.tv_sec))
                         .arg(qint64(st.st_mtim.tv_nsec)));
    }
    return lines;
}

QStringList namesIn(const QString &folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

QString textOf(QQuickItem *item)
{
    return item ? item->property("text").toString() : QString();
}

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

// The row of an open menu level with this label, or null.
QQuickItem *menuRow(UiHarness &ui, int index, const QString &label)
{
    QList<QQuickItem *> items;
    collect(ui.view(u"menuLevel%1"_s.arg(index)), u"menuItem"_s, items);
    for (QQuickItem *item : std::as_const(items)) {
        if (item->property("label").toString() == label)
            return item;
    }
    return nullptr;
}

QQuickItem *menuTitle(UiHarness &ui, const QString &title)
{
    QList<QQuickItem *> titles;
    collect(ui.item(u"menuBar"_s), u"menuTitle"_s, titles);
    for (QQuickItem *item : std::as_const(titles)) {
        if (item->property("title").toString() == title)
            return item;
    }
    return nullptr;
}

// Clicks the middle of item the way a person would: only once the window has laid out what it
// shows. An item that has just appeared can still sit where it was before its column placed it,
// and a click there lands on something else.
bool click(UiHarness &ui, QQuickItem *item, Qt::MouseButton button = Qt::LeftButton)
{
    if (!item || !item->isVisible() || !QQuickTest::qWaitForPolish(ui.window()))
        return false;
    QTest::mouseClick(ui.window(), button, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    return true;
}

QStringList descriptions(QQuickItem *view)
{
    QStringList texts;
    const int count = view ? view->property("count").toInt() : 0;
    for (int row = 0; row < count; ++row) {
        QQuickItem *delegate = UiHarness::delegateAt(view, row);
        texts.append(delegate ? delegate->property("description").toString() : u"?"_s);
    }
    return texts;
}

const QStringList docVersions = {u"2026-11-02 10:00"_s, u"2026-11-01 10:00"_s};

} // namespace

class VersionsUiTests : public QObject
{
    Q_OBJECT

private slots:
    void uiPreviousVersions();
    void versionsForFolders();
    void snapshotsAreReadOnly();
    void versionsHiddenWithoutSnapshots();
};

void VersionsUiTests::uiPreviousVersions()
{
    UiHarness ui;
    const QString root = ui.root();
    const QString doc = root + u"/doc.txt"_s;
    QVERIFY(makeSnapshots(root));
    QVERIFY(ui.start({doc}));
    Browser &browser = ui.browser();
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"doc.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"doc.txt"_s);
    QTRY_VERIFY(browser.actions()->isEnabled(u"previous-versions"_s));

    // Alt+V lists the versions, newest first, with their dates and descriptions.
    ui.press(Qt::Key_V, Qt::AltModifier);
    QTRY_VERIFY(ui.view(u"versions"_s));
    QQuickItem *versions = ui.view(u"versions"_s);
    QTRY_COMPARE(UiHarness::shownNames(versions), docVersions);
    QCOMPARE(descriptions(versions), (QStringList{u"timeline"_s, u"timeline"_s}));
    QVERIFY(!ui.view(u"list"_s));

    // Escape closes the list: the folder again, the keys on doc.txt.
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"versions"_s));
    QTRY_VERIFY(ui.view(u"list"_s));
    QCOMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"doc.txt"_s);

    // File > Previous Versions, with the mouse.
    QVERIFY(click(ui, menuTitle(ui, u"File"_s)));
    QTRY_VERIFY(menuRow(ui, 0, u"Previous Versions"_s));
    QCOMPARE(menuRow(ui, 0, u"Previous Versions"_s)->property("shortcut").toString(), u"Alt+V"_s);
    QVERIFY(menuRow(ui, 0, u"Previous Versions"_s)->property("usable").toBool());
    QVERIFY(click(ui, menuRow(ui, 0, u"Previous Versions"_s)));
    QTRY_VERIFY(ui.view(u"versions"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"versions"_s)), docVersions);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"versions"_s));

    // The details pane has a section about them, and its button shows the list.
    ui.press(Qt::Key_I, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"detailsVersions"_s));
    QTRY_COMPARE(textOf(ui.view(u"detailsVersionsCount"_s)), u"2 earlier versions"_s);
    QVERIFY(click(ui, ui.view(u"detailsVersionsButton"_s)));
    QTRY_VERIFY(ui.view(u"versions"_s));
    versions = ui.view(u"versions"_s);
    QTRY_COMPARE(UiHarness::shownNames(versions), docVersions);
    QTRY_COMPARE(UiHarness::currentName(versions), u"2026-11-02 10:00"_s);

    // Return on 2026-11-01 asks first; Escape says no and nothing changes.
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(UiHarness::currentName(versions), u"2026-11-01 10:00"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(ui.view(u"question"_s));
    QCOMPARE(textOf(ui.view(u"questionText"_s)), u"Restore this version?"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"question"_s));
    QTest::qWait(200);
    QCOMPARE(readFile(doc), QByteArray("v3"));
    QVERIFY(ui.view(u"versions"_s));

    // Yes restores it: doc.txt is "v1", and the "v3" it was is in the trash.
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(ui.view(u"question"_s));
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(readFile(doc), QByteArray("v1"));
    const QString trashFiles = ui.scratch() + u"/data/Trash/files"_s;
    QTRY_COMPARE(namesIn(trashFiles), QStringList{u"doc.txt"_s});
    QCOMPARE(readFile(trashFiles + u"/doc.txt"_s), QByteArray("v3"));
    QTRY_VERIFY(!ui.view(u"versions"_s));
    QTRY_VERIFY(ui.view(u"list"_s));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"doc.txt"_s);

    // One undo gives "v3" back.
    QTRY_VERIFY(browser.ops()->canUndo());
    QCOMPARE(browser.ops()->undoText(), u"Undo Restore Version"_s);
    ui.press(Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(readFile(doc), QByteArray("v3"));

    // Restore as Copy, from the context menu of the 2026-11-01 row (a right click on it).
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"doc.txt"_s);
    QTRY_VERIFY(browser.actions()->isEnabled(u"previous-versions"_s));
    ui.press(Qt::Key_V, Qt::AltModifier);
    QTRY_VERIFY(ui.view(u"versions"_s));
    versions = ui.view(u"versions"_s);
    QTRY_COMPARE(UiHarness::shownNames(versions), docVersions);
    QVERIFY(click(ui, UiHarness::delegateAt(versions, 1), Qt::RightButton));
    QTRY_VERIFY(menuRow(ui, 0, u"Restore as Copy"_s));
    QCOMPARE(UiHarness::currentName(versions), u"2026-11-01 10:00"_s);
    QVERIFY(click(ui, menuRow(ui, 0, u"Restore as Copy"_s)));
    const QString copy = root + u"/doc (2026-11-01 10.00).txt"_s;
    QTRY_VERIFY(QFileInfo::exists(copy));
    QTRY_COMPARE(readFile(copy), QByteArray("v1"));
    QCOMPARE(readFile(doc), QByteArray("v3"));
    QTRY_VERIFY(ui.view(u"list"_s));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"doc (2026-11-01 10.00).txt"_s);

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void VersionsUiTests::versionsForFolders()
{
    UiHarness ui;
    const QString root = ui.root();
    QVERIFY(makeSnapshots(root));
    const QStringList before = fingerprint(root + u"/.snapshots"_s);
    QVERIFY(ui.start({root}));
    Browser &browser = ui.browser();
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"doc.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"D"_s);
    QTRY_VERIFY(browser.actions()->isEnabled(u"previous-versions"_s));

    // The versions of D: one per snapshot holding it.
    ui.press(Qt::Key_V, Qt::AltModifier);
    QTRY_VERIFY(ui.view(u"versions"_s));
    QQuickItem *versions = ui.view(u"versions"_s);
    QTRY_COMPARE(UiHarness::shownNames(versions), docVersions);

    // Return on the newest opens it in the normal views: [a.txt], read-only.
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(browser.folder(), snap(root, 2) + u"/D"_s);
    QTRY_VERIFY(!ui.view(u"versions"_s));
    list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), QStringList{u"a.txt"_s});
    QVERIFY(browser.model()->isReadOnly());
    QVERIFY(browser.model()->property("readOnly").toBool());
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);
    QVERIFY(!browser.actions()->isEnabled(u"rename"_s));
    QVERIFY(!browser.actions()->isEnabled(u"trash"_s));
    QVERIFY(!browser.actions()->isEnabled(u"new-folder"_s));
    QVERIFY(!browser.actions()->isEnabled(u"paste"_s));
    QVERIFY(!browser.actions()->isEnabled(u"previous-versions"_s));

    // Copying a.txt out of it to root works.
    ui.press(Qt::Key_C, Qt::ControlModifier);
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(browser.folder(), root);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"D"_s, u"doc.txt"_s}));
    ui.press(Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(readFile(root + u"/a.txt"_s), QByteArray("a"));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"D"_s, u"a.txt"_s, u"doc.txt"_s}));
    QCOMPARE(fingerprint(root + u"/.snapshots"_s), before);

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void VersionsUiTests::snapshotsAreReadOnly()
{
    UiHarness ui;
    const QString root = ui.root();
    QVERIFY(makeSnapshots(root));
    QVERIFY(writeFile(ui.scratch() + u"/elsewhere/x.txt"_s, "x"));
    const QStringList before = fingerprint(root + u"/.snapshots"_s);
    const QString message = u"Snapshots can't be changed."_s;

    // x.txt on the clipboard, from a folder that is not a snapshot.
    QVERIFY(ui.start({ui.scratch() + u"/elsewhere/x.txt"_s}));
    Browser &browser = ui.browser();
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"x.txt"_s);
    ui.press(Qt::Key_C, Qt::ControlModifier);

    // Snapshot 1's folder, the keys on doc.txt.
    browser.open(snap(root, 1), snap(root, 1) + u"/doc.txt"_s);
    QQuickItem *list = ui.view(u"list"_s);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"doc.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"doc.txt"_s);
    QVERIFY(browser.model()->isReadOnly());

    const auto refused = [&](int key, Qt::KeyboardModifiers mods) {
        browser.ops()->dismissMessage();
        if (!browser.ops()->message().isEmpty())
            return false;
        ui.press(key, mods);
        if (!QTest::qWaitFor([&] { return browser.ops()->message() == message; }, 5000))
            return false;
        const bool shown = textOf(ui.view(u"messageText"_s)) == message;
        return shown && !browser.ops()->editing() && browser.question().isEmpty();
    };
    QVERIFY2(refused(Qt::Key_F2, Qt::NoModifier), "rename");
    QVERIFY2(refused(Qt::Key_Delete, Qt::NoModifier), "move to trash");
    QVERIFY2(refused(Qt::Key_Delete, Qt::ShiftModifier), "delete for good");
    QVERIFY2(refused(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier), "new folder");
    QVERIFY2(refused(Qt::Key_V, Qt::ControlModifier), "paste");
    QVERIFY2(refused(Qt::Key_X, Qt::ControlModifier), "cut");

    // The same inside a folder of the snapshot.
    ui.press(Qt::Key_Home);
    QTRY_COMPARE(UiHarness::currentName(list), u"D"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(browser.folder(), snap(root, 1) + u"/D"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), QStringList{u"a.txt"_s});
    QVERIFY2(refused(Qt::Key_V, Qt::ControlModifier), "paste into a folder of it");
    QVERIFY2(refused(Qt::Key_F2, Qt::NoModifier), "rename in a folder of it");

    // The menus grey them out.
    QVERIFY(click(ui, menuTitle(ui, u"File"_s)));
    QTRY_VERIFY(menuRow(ui, 0, u"Rename"_s));
    QVERIFY(!menuRow(ui, 0, u"Rename"_s)->property("usable").toBool());
    QVERIFY(!menuRow(ui, 0, u"Move to Trash"_s)->property("usable").toBool());
    QVERIFY(!menuRow(ui, 0, u"New Folder"_s)->property("usable").toBool());
    ui.press(Qt::Key_Escape);

    // Nothing under .snapshots changed, and nothing was pasted anywhere in it.
    QTest::qWait(500);
    QCOMPARE(fingerprint(root + u"/.snapshots"_s), before);
    QCOMPARE(namesIn(ui.scratch() + u"/data/Trash/files"_s), QStringList());
    QCOMPARE(readFile(ui.scratch() + u"/elsewhere/x.txt"_s), QByteArray("x"));

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void VersionsUiTests::versionsHiddenWithoutSnapshots()
{
    UiHarness ui;
    // A folder with no .snapshots anywhere above it: in /dev/shm when it can be written there,
    // so a machine whose own file system has snapshots does not change the result.
    std::unique_ptr<QTemporaryDir> shm;
    if (QFileInfo(u"/dev/shm"_s).isWritable())
        shm = std::make_unique<QTemporaryDir>(u"/dev/shm/riley-versions-XXXXXX"_s);
    const QString folder = shm && shm->isValid() ? shm->path() : ui.root();
    const QString plain = folder + u"/plain.txt"_s;
    QVERIFY(writeFile(plain, "plain"));
    QVERIFY(Riley::Snapshots::baseFor(plain).isEmpty());

    QVERIFY(ui.start({plain}));
    Browser &browser = ui.browser();
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"plain.txt"_s);
    Riley::VersionModel *versions = browser.versions();
    QTRY_VERIFY(versions->path() == plain && !versions->isLoading());
    QVERIFY(!versions->isAvailable());

    // The menu item is greyed out, and Alt+V shows nothing.
    QVERIFY(!browser.actions()->isEnabled(u"previous-versions"_s));
    QVERIFY(click(ui, menuTitle(ui, u"File"_s)));
    QTRY_VERIFY(menuRow(ui, 0, u"Previous Versions"_s));
    QVERIFY(!menuRow(ui, 0, u"Previous Versions"_s)->property("usable").toBool());
    ui.press(Qt::Key_Escape);
    ui.press(Qt::Key_V, Qt::AltModifier);
    QTest::qWait(200);
    QVERIFY(!ui.view(u"versions"_s));
    QVERIFY(ui.view(u"list"_s));

    // The details pane has no section about them.
    ui.press(Qt::Key_I, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"details"_s));
    QTRY_COMPARE(textOf(ui.view(u"detailsName"_s)), u"plain.txt"_s);
    QTest::qWait(200);
    QVERIFY(ui.item(u"detailsVersions"_s));
    QVERIFY(!ui.view(u"detailsVersions"_s));

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

QTEST_MAIN(VersionsUiTests)
#include "versionsui_tests.moc"
