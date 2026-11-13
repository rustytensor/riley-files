// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// File operations in the main window, as a person does them: the real QML and C++, real files,
// keys. Expected results are the ones written down in the milestone's test list. The trash is
// pointed at a scratch folder (XDG_DATA_HOME), so no test touches a real one.

#include "browser.h"
#include "fileops.h"
#include "uiharness.h"

#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

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
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<unreadable>");
}

// The names shown, sorted, for checks that do not care about the order.
QStringList sortedNames(QQuickItem *view)
{
    QStringList names = UiHarness::shownNames(view);
    names.sort();
    return names;
}

QString textOf(QQuickItem *item)
{
    return item ? item->property("text").toString() : QString();
}

bool chosen(const UiHarness &ui, const QString &button)
{
    QQuickItem *item = ui.view(button);
    return item && item->property("chosen").toBool();
}

} // namespace

class FileUiTests : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void uiCopyPasteFiles();
    void uiCutPasteMoves();
    void uiRenameWithF2();
    void uiNewFolder();
    void uiDeleteGoesToTrash();
    void uiClashDialogAsks();
    void uiLongCopyCanBeStopped();
    void uiClipboardSharedWithOtherApps();

private:
    // Each test's trash lives in its own scratch folder.
    void useScratchTrash(const UiHarness &ui);
};

void FileUiTests::init()
{
    QGuiApplication::clipboard()->clear();
}

void FileUiTests::useScratchTrash(const UiHarness &ui)
{
    qputenv("XDG_DATA_HOME", QDir(ui.scratch()).filePath(u"data"_s).toUtf8());
}

void FileUiTests::uiCopyPasteFiles()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QString a = QDir(ui.root()).filePath(u"a.txt"_s);
    const QString d = QDir(ui.root()).filePath(u"D"_s);
    QVERIFY(writeFile(a, "alpha"));
    QVERIFY(QDir().mkpath(d));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"D"_s);
    ui.press(Qt::Key_Down);
    QCOMPARE(UiHarness::currentName(list), u"a.txt"_s);
    ui.press(Qt::Key_C, Qt::ControlModifier);

    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), d);
    ui.press(Qt::Key_V, Qt::ControlModifier);

    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);
    QCOMPARE(readFile(d + u"/a.txt"_s), QByteArray("alpha"));
    QCOMPARE(readFile(a), QByteArray("alpha")); // the source stays

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiCutPasteMoves()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QString a = QDir(ui.root()).filePath(u"a.txt"_s);
    const QString d = QDir(ui.root()).filePath(u"D"_s);
    QVERIFY(writeFile(a, "alpha"));
    QVERIFY(QDir().mkpath(d));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"D"_s);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_X, Qt::ControlModifier);
    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), d);
    ui.press(Qt::Key_V, Qt::ControlModifier);

    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);
    QCOMPARE(readFile(d + u"/a.txt"_s), QByteArray("alpha"));
    QVERIFY(!QFileInfo::exists(a));

    ui.press(Qt::Key_Backspace);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s}));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiRenameWithF2()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QDir root(ui.root());
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "alpha"));
    QVERIFY(writeFile(root.filePath(u"b.txt"_s), "beta"));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);

    // F2, a new name, Return.
    ui.press(Qt::Key_F2);
    QTRY_VERIFY(ui.view(u"renameField"_s));
    QCOMPARE(textOf(ui.view(u"renameField"_s)), u"a.txt"_s);
    ui.type(u"z.txt"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"b.txt"_s, u"z.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"z.txt"_s);
    QVERIFY(!ui.view(u"renameField"_s));
    QCOMPARE(readFile(root.filePath(u"z.txt"_s)), QByteArray("alpha"));

    // F2 then Escape: unchanged.
    ui.press(Qt::Key_F2);
    QTRY_VERIFY(ui.view(u"renameField"_s));
    ui.type(u"q"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"renameField"_s));
    QTest::qWait(200);
    QCOMPARE(UiHarness::shownNames(list), (QStringList{u"b.txt"_s, u"z.txt"_s}));
    QCOMPARE(UiHarness::currentName(list), u"z.txt"_s);
    QCOMPARE(root.entryList(QDir::Files), (QStringList{u"b.txt"_s, u"z.txt"_s}));

    // A name already taken: a message, nothing renamed.
    ui.press(Qt::Key_F2);
    QTRY_VERIFY(ui.view(u"renameField"_s));
    ui.type(u"b.txt"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(ui.view(u"message"_s));
    QVERIFY2(textOf(ui.item(u"messageText"_s)).contains(u"“b.txt”"_s), qPrintable(textOf(ui.item(u"messageText"_s))));
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"renameField"_s));
    QTest::qWait(200);
    QCOMPARE(root.entryList(QDir::Files), (QStringList{u"b.txt"_s, u"z.txt"_s}));
    QCOMPARE(readFile(root.filePath(u"b.txt"_s)), QByteArray("beta"));
    QCOMPARE(readFile(root.filePath(u"z.txt"_s)), QByteArray("alpha"));
    QCOMPARE(UiHarness::shownNames(list), (QStringList{u"b.txt"_s, u"z.txt"_s}));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiNewFolder()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QDir root(ui.root());
    QVERIFY(writeFile(root.filePath(u"e.txt"_s), "e"));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"e.txt"_s}));

    // Made, with its name being edited.
    ui.press(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"New Folder"_s, u"e.txt"_s}));
    QTRY_VERIFY(ui.view(u"renameField"_s));
    QCOMPARE(textOf(ui.view(u"renameField"_s)), u"New Folder"_s);
    QCOMPARE(UiHarness::currentName(list), u"New Folder"_s);
    QVERIFY(QFileInfo(root.filePath(u"New Folder"_s)).isDir());
    QTest::qWait(100);

    ui.type(u"Docs"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Docs"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"Docs"_s);
    QVERIFY(QFileInfo(root.filePath(u"Docs"_s)).isDir());
    QVERIFY(!QFileInfo::exists(root.filePath(u"New Folder"_s)));

    // With "New Folder" already there, the next is "New Folder (2)".
    ui.press(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(ui.view(u"renameField"_s));
    QCOMPARE(textOf(ui.view(u"renameField"_s)), u"New Folder"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"renameField"_s));
    ui.press(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(ui.view(u"renameField"_s));
    QCOMPARE(textOf(ui.view(u"renameField"_s)), u"New Folder (2)"_s);
    QTRY_COMPARE(UiHarness::currentName(list), u"New Folder (2)"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"renameField"_s));
    QTRY_COMPARE(sortedNames(list),
                 (QStringList{u"Docs"_s, u"New Folder"_s, u"New Folder (2)"_s, u"e.txt"_s}));
    QVERIFY(QFileInfo(root.filePath(u"New Folder (2)"_s)).isDir());

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiDeleteGoesToTrash()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QDir root(ui.root());
    const QString trashed = QDir(ui.scratch()).filePath(u"data/Trash/files/a.txt"_s);
    const QString info = QDir(ui.scratch()).filePath(u"data/Trash/info/a.txt.trashinfo"_s);
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "alpha"));
    QVERIFY(writeFile(root.filePath(u"b.txt"_s), "beta"));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);

    ui.press(Qt::Key_Delete);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"b.txt"_s}));
    QVERIFY(!QFileInfo::exists(root.filePath(u"a.txt"_s)));
    QCOMPARE(readFile(trashed), QByteArray("alpha"));
    QVERIFY(QFileInfo::exists(info));

    ui.press(Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);
    QCOMPARE(readFile(root.filePath(u"a.txt"_s)), QByteArray("alpha"));
    QVERIFY(!QFileInfo::exists(trashed));
    QVERIFY(!QFileInfo::exists(info));

    ui.press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"b.txt"_s}));
    QVERIFY(!QFileInfo::exists(root.filePath(u"a.txt"_s)));
    QCOMPARE(readFile(trashed), QByteArray("alpha"));
    QVERIFY(QFileInfo::exists(info));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiClashDialogAsks()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QDir root(ui.root());
    const QString d = root.filePath(u"D"_s);
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "new"));
    QVERIFY(writeFile(d + u"/a.txt"_s, "old"));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"D"_s);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_C, Qt::ControlModifier);
    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), d);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s}));
    ui.press(Qt::Key_V, Qt::ControlModifier);

    // The question, with its three answers.
    QTRY_VERIFY(ui.view(u"clashDialog"_s));
    QCOMPARE(textOf(ui.view(u"skipButton"_s)), u"Skip"_s);
    QCOMPARE(textOf(ui.view(u"keepBothButton"_s)), u"Keep both"_s);
    QCOMPARE(textOf(ui.view(u"replaceButton"_s)), u"Replace"_s);
    QVERIFY(textOf(ui.item(u"clashTitle"_s)).contains(u"“a.txt”"_s));
    // Nothing has changed while it asks.
    QCOMPARE(QDir(d).entryList(QDir::Files), (QStringList{u"a.txt"_s}));
    QCOMPARE(readFile(d + u"/a.txt"_s), QByteArray("old"));

    // Reached by keys.
    QVERIFY(chosen(ui, u"keepBothButton"_s));
    ui.press(Qt::Key_Left);
    QVERIFY(chosen(ui, u"skipButton"_s));
    ui.press(Qt::Key_Right);
    ui.press(Qt::Key_Right);
    QVERIFY(chosen(ui, u"replaceButton"_s));
    ui.press(Qt::Key_Left);
    QVERIFY(chosen(ui, u"keepBothButton"_s));
    ui.press(Qt::Key_Return);

    QTRY_VERIFY(!ui.view(u"clashDialog"_s));
    QTRY_COMPARE(sortedNames(list), (QStringList{u"a (2).txt"_s, u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"a (2).txt"_s);
    QCOMPARE(readFile(d + u"/a.txt"_s), QByteArray("old"));
    QCOMPARE(readFile(d + u"/a (2).txt"_s), QByteArray("new"));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiLongCopyCanBeStopped()
{
    UiHarness ui;
    useScratchTrash(ui);
    constexpr int files = 5000;
    const QDir root(ui.root());
    const QString src = root.filePath(u"src"_s);
    const QString dest = root.filePath(u"dest"_s);
    QVERIFY(QDir().mkpath(src));
    QVERIFY(QDir().mkpath(dest));
    const QByteArray block(4096, 'r');
    for (int i = 0; i < files; ++i) {
        QFile file(src + u"/f%1"_s.arg(i, 4, 10, QLatin1Char('0')));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(block), qint64(block.size()));
    }
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"dest"_s, u"src"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"dest"_s);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_C, Qt::ControlModifier);
    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), dest);

    QElapsedTimer clock;
    clock.start();
    ui.press(Qt::Key_V, Qt::ControlModifier);
    QQuickItem *stop = nullptr;
    while (!(stop = ui.view(u"stopButton"_s)) && clock.elapsed() < 1000)
        QTest::qWait(5);
    QVERIFY2(stop, "no progress with a Stop button within 1 s");
    QVERIFY(ui.view(u"progress"_s));
    qInfo("progress shown after %lld ms", clock.elapsed());

    const QPointF centre = stop->mapToScene(QPointF(stop->width() / 2, stop->height() / 2));
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier, centre.toPoint());

    QTRY_VERIFY_WITH_TIMEOUT(!ui.view(u"progress"_s), 2000);
    QTRY_COMPARE(ui.browser().ops()->queue().activeCount(), 0);

    // It ended before the end, and left no partial file.
    int copied = 0;
    for (QDirIterator it(dest + u"/src"_s, QDir::Files | QDir::Hidden | QDir::System); it.hasNext();) {
        const QFileInfo info = it.nextFileInfo();
        QCOMPARE(info.size(), qint64(block.size()));
        ++copied;
    }
    qInfo("%d of %d files copied before Stop", copied, files);
    QVERIFY2(copied < files, "the copy was not stopped");
    QCOMPARE(QDir(src).entryList(QDir::Files).size(), files);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void FileUiTests::uiClipboardSharedWithOtherApps()
{
    UiHarness ui;
    useScratchTrash(ui);
    const QDir root(ui.root());
    const QString a = root.filePath(u"a.txt"_s);
    const QString d = root.filePath(u"D"_s);
    const QString other = QDir(ui.scratch()).filePath(u"elsewhere/x.txt"_s);
    QVERIFY(writeFile(a, "alpha"));
    QVERIFY(QDir().mkpath(d));
    QVERIFY(writeFile(other, "from another program"));
    QVERIFY(ui.start({ui.root()}));

    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"a.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"D"_s);
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_C, Qt::ControlModifier);

    // What other programs read. The scratch path holds no character that needs escaping, so its
    // URI is "file://" and the path as it is.
    QVERIFY2(!a.contains(u' ') && !a.contains(u'%'), qPrintable(a));
    const QByteArray uri = "file://" + a.toUtf8();
    const QMimeData *held = QGuiApplication::clipboard()->mimeData();
    QVERIFY(held);
    QVERIFY(held->hasFormat(u"text/uri-list"_s));
    QCOMPARE(held->data(u"text/uri-list"_s).trimmed(), uri);
    QCOMPARE(held->data(u"x-special/gnome-copied-files"_s), "copy\n" + uri);

    // A uri-list put there by another program pastes.
    auto *fromOther = new QMimeData;
    fromOther->setData(u"text/uri-list"_s, "file://" + other.toUtf8() + "\r\n");
    QGuiApplication::clipboard()->setMimeData(fromOther);
    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Return);
    QCOMPARE(ui.browser().folder(), d);
    ui.press(Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"x.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"x.txt"_s);
    QCOMPARE(readFile(d + u"/x.txt"_s), QByteArray("from another program"));
    QCOMPARE(readFile(other), QByteArray("from another program"));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(FileUiTests)
#include "fileui_tests.moc"
