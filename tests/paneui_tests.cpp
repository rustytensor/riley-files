// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The sidebar, the details pane and the preview in the main window, driven with keys through
// the real window on real files. Expected results are the ones written down in the milestone's
// test list.

#include "browser.h"
#include "uiharness.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQuickItem>
#include <QTest>
#include <QTimeZone>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeImage(const QString &path, int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(qRgb(40, 120, 200));
    return QDir().mkpath(QFileInfo(path).absolutePath()) && image.save(path, "png");
}

QString textOf(QQuickItem *item)
{
    return item ? item->property("text").toString() : QString();
}

// A delegate's bool property on a row of a sidebar list.
bool rowFlag(QQuickItem *view, int row, const char *property)
{
    QQuickItem *delegate = UiHarness::delegateAt(view, row);
    return delegate && delegate->property(property).toBool();
}

} // namespace

class PaneUiTests : public QObject
{
    Q_OBJECT

private slots:
    void uiSidebarNavigates();
    void detailsShowWhatItIs();
    void uiPreviewWithSpace();
};

void PaneUiTests::uiSidebarNavigates()
{
    UiHarness ui;
    // A home of its own, with Documents and Pictures, named in user-dirs.dirs.
    const QDir scratch(ui.scratch());
    const QString home = scratch.filePath(u"home"_s);
    QVERIFY(QDir().mkpath(home + u"/Documents"_s));
    QVERIFY(QDir().mkpath(home + u"/Pictures"_s));
    QVERIFY(writeFile(scratch.filePath(u"config/user-dirs.dirs"_s),
                      "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\nXDG_PICTURES_DIR=\"$HOME/Pictures\"\n"));
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.toUtf8());
    qputenv("XDG_DATA_HOME", scratch.filePath(u"data"_s).toUtf8());
    QVERIFY(ui.makeFile(u"a.txt"_s));
    QVERIFY(ui.makeFile(u"b.txt"_s));
    const QString bookmarks = scratch.filePath(u"config/gtk-3.0/bookmarks"_s);
    const QByteArray rootLine = QUrl::fromLocalFile(ui.root()).toEncoded();

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *places = ui.view(u"places"_s);
    QVERIFY(places);
    QTRY_COMPARE(UiHarness::shownNames(places),
                 (QStringList{u"Home"_s, u"Documents"_s, u"Pictures"_s, u"Trash"_s}));
    QQuickItem *pinned = ui.item(u"pinned"_s);
    QVERIFY(pinned);
    QCOMPARE(pinned->property("count").toInt(), 0);
    QCOMPARE(ui.browser().markedPlace(), -1);

    // Ctrl+D pins the folder shown; it is marked in the sidebar.
    ui.press(Qt::Key_D, Qt::ControlModifier);
    QTRY_COMPARE(UiHarness::shownNames(pinned), (QStringList{u"root"_s}));
    QVERIFY(readFile(bookmarks).split('\n').contains(rootLine));
    QTRY_VERIFY(rowFlag(pinned, 0, "marked"));

    // Tab puts the keys in the sidebar, on the folder shown; Up twice is Pictures, then Documents.
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"a.txt"_s);
    ui.press(Qt::Key_Tab);
    QVERIFY(ui.browser().sidebarFocus());
    QCOMPARE(ui.browser().sidebarCursor(), 4);
    QVERIFY(rowFlag(pinned, 0, "current"));
    ui.press(Qt::Key_Up);
    QVERIFY(rowFlag(places, 3, "current"));
    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Up);
    QVERIFY(rowFlag(places, 1, "current"));
    // The files' cursor did not move meanwhile.
    QCOMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"a.txt"_s);

    // Return opens it, marks it, and gives the keys back to the files.
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), home + u"/Documents"_s);
    QVERIFY(!ui.browser().sidebarFocus());
    QTRY_VERIFY(rowFlag(places, 1, "marked"));
    QVERIFY(!rowFlag(pinned, 0, "marked"));
    QVERIFY(!rowFlag(places, 1, "current"));

    // Back to the pinned folder from the keys: Tab, Down to its row, Return.
    ui.press(Qt::Key_Tab);
    QCOMPARE(ui.browser().sidebarCursor(), 1);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_Down);
    QVERIFY(rowFlag(pinned, 0, "current"));
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), ui.root());
    QTRY_VERIFY(rowFlag(pinned, 0, "marked"));
    QVERIFY(!rowFlag(places, 1, "marked"));

    // Tab there and back: the keys move the files' cursor again.
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"a.txt"_s);
    ui.press(Qt::Key_Tab);
    ui.press(Qt::Key_Tab);
    QVERIFY(!ui.browser().sidebarFocus());
    ui.press(Qt::Key_Down);
    QCOMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"b.txt"_s);

    // Ctrl+D again unpins it, and takes its line out of the file.
    ui.press(Qt::Key_D, Qt::ControlModifier);
    QTRY_COMPARE(pinned->property("count").toInt(), 0);
    QVERIFY(!readFile(bookmarks).split('\n').contains(rootLine));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
    ui.stop();
    qputenv("HOME", oldHome);
}

void PaneUiTests::detailsShowWhatItIs()
{
    UiHarness ui;
    const QDir root(ui.root());
    const QString a = root.filePath(u"a.txt"_s);
    QVERIFY(writeFile(a, QByteArray(300, 'x')));
    QVERIFY(QFile::setPermissions(a, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser
                                         | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    const QDateTime when(QDate(2020, 1, 1), QTime(12, 0), QTimeZone::UTC);
    {
        QFile file(a);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(when, QFileDevice::FileModificationTime));
    }
    QVERIFY(ui.makeFile(u"F/one"_s));
    QVERIFY(ui.makeFile(u"F/two"_s));
    QVERIFY(ui.makeFile(u"F/three"_s));
    QVERIFY(writeImage(root.filePath(u"p.png"_s), 64, 48));

    // The date as the pane writes it, in local time, worked out here by hand.
    const QDateTime local = when.toLocalTime();
    static const char *const months[] = {"January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December"};
    const QString date = u"%1 %2 %3, %4:%5"_s.arg(local.date().day())
                             .arg(QLatin1String(months[local.date().month() - 1]))
                             .arg(local.date().year())
                             .arg(local.time().hour(), 2, 10, QLatin1Char('0'))
                             .arg(local.time().minute(), 2, 10, QLatin1Char('0'));

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"F"_s, u"a.txt"_s, u"p.png"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"F"_s);

    // Hidden at first; Ctrl+I shows it, and the header's button is lit.
    QVERIFY(!ui.view(u"details"_s));
    ui.press(Qt::Key_I, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"details"_s));
    QVERIFY(ui.item(u"detailsButton"_s)->property("on").toBool());

    // The folder under the cursor.
    QTRY_COMPARE(textOf(ui.item(u"detailsName"_s)), u"F"_s);
    QTRY_COMPARE(textOf(ui.item(u"detailsSize"_s)), u"3 items"_s);
    QCOMPARE(textOf(ui.item(u"detailsKind"_s)), u"Folder"_s);

    // It follows the cursor.
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(textOf(ui.item(u"detailsName"_s)), u"a.txt"_s);
    QCOMPARE(textOf(ui.item(u"detailsSize"_s)), u"300 bytes"_s);
    QCOMPARE(textOf(ui.item(u"detailsPermissions"_s)), u"rw-r--r--"_s);
    QCOMPARE(textOf(ui.item(u"detailsModified"_s)), date);
    QVERIFY(!ui.view(u"detailsImageSize"_s)); // not a picture

    ui.press(Qt::Key_Down);
    QTRY_COMPARE(textOf(ui.item(u"detailsName"_s)), u"p.png"_s);
    QCOMPARE(textOf(ui.item(u"detailsImageSize"_s)), u"64 × 48"_s);
    QVERIFY(ui.view(u"detailsImageSize"_s));

    // Ctrl+I hides it again.
    ui.press(Qt::Key_I, Qt::ControlModifier);
    QTRY_VERIFY(!ui.view(u"details"_s));
    QVERIFY(!ui.item(u"detailsButton"_s)->property("on").toBool());

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void PaneUiTests::uiPreviewWithSpace()
{
    UiHarness ui;
    const QDir root(ui.root());
    QVERIFY(writeImage(root.filePath(u"big.png"_s), 4000, 1000));
    QVERIFY(writeImage(root.filePath(u"p.png"_s), 64, 48));
    QByteArray text;
    for (int i = 1; i <= 300; ++i)
        text += "line " + QByteArray::number(i) + '\n';
    QVERIFY(writeFile(root.filePath(u"q.txt"_s), text));

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"big.png"_s, u"p.png"_s, u"q.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"big.png"_s);
    QVERIFY(!ui.view(u"preview"_s));

    // A picture larger than the window is fitted, its shape kept.
    ui.press(Qt::Key_Space);
    QTRY_VERIFY(ui.view(u"preview"_s));
    QQuickItem *image = ui.item(u"previewImage"_s);
    QVERIFY(image);
    QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
    const qreal room = image->parentItem()->width();
    QVERIFY(room > 100 && room < 4000);
    QTRY_COMPARE(image->property("paintedWidth").toReal(), room);
    QVERIFY(qAbs(image->property("paintedHeight").toReal() - room / 4) <= 1.0);

    // Down: the next file, a small picture, shown at its own size.
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(textOf(ui.item(u"previewName"_s)), u"p.png"_s);
    QTRY_COMPARE(image->property("paintedWidth").toReal(), 64.0);
    QCOMPARE(image->property("paintedHeight").toReal(), 48.0);
    QCOMPARE(UiHarness::currentName(list), u"p.png"_s);

    // Down again: a text file shows its first lines (200 of its 300).
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(textOf(ui.item(u"previewName"_s)), u"q.txt"_s);
    QVERIFY(ui.view(u"previewText"_s));
    const QStringList lines = textOf(ui.item(u"previewText"_s)).split(u'\n');
    QCOMPARE(lines.size(), 200);
    QCOMPARE(lines.first(), u"line 1"_s);
    QCOMPARE(lines.last(), u"line 200"_s);
    QVERIFY(!ui.view(u"previewImage"_s));

    // Up goes back; Escape closes it, and the keys move the files' cursor again.
    ui.press(Qt::Key_Up);
    QTRY_COMPARE(textOf(ui.item(u"previewName"_s)), u"p.png"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"preview"_s));
    ui.press(Qt::Key_Down);
    QCOMPARE(UiHarness::currentName(list), u"q.txt"_s);

    // Space opens it on the file under the cursor and Space closes it.
    ui.press(Qt::Key_Space);
    QTRY_VERIFY(ui.view(u"preview"_s));
    QCOMPARE(textOf(ui.item(u"previewName"_s)), u"q.txt"_s);
    ui.press(Qt::Key_Space);
    QTRY_VERIFY(!ui.view(u"preview"_s));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(PaneUiTests)
#include "paneui_tests.moc"
