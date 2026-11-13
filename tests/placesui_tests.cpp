// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The places that are not folders, driven through the real window: the trash view (Restore,
// Delete for Good, Empty Trash), the recent files read from and written to recently-used.xbel,
// Connect to Server and the Network section, and the sidebar's sections that show only when
// they have something. Expected results are the ones written down in the milestone's test list.

#include "browser.h"
#include "trash.h"
#include "uiharness.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QUrl>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool appendFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::Append) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString textOf(QQuickItem *item)
{
    return item ? item->property("text").toString() : QString();
}

QStringList namesIn(const QString &folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

bool clickItem(UiHarness &ui, QQuickItem *item)
{
    if (!item || !item->isVisible())
        return false;
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    return true;
}

bool shown(UiHarness &ui, const QString &objectName)
{
    return ui.view(objectName) != nullptr;
}

// Every file named PWNED below folder.
QStringList pwnedBelow(const QString &folder)
{
    QStringList found;
    QDirIterator it(folder, {u"PWNED"_s}, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        found.append(it.next());
    return found;
}

class EnvKeeper
{
public:
    explicit EnvKeeper(const char *name)
        : m_name(name)
        , m_had(qEnvironmentVariableIsSet(name))
        , m_value(qgetenv(name))
    {
    }
    ~EnvKeeper()
    {
        if (m_had)
            qputenv(m_name, m_value);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name;
    bool m_had;
    QByteArray m_value;
};

const QByteArray rootOnly = "22 1 253:1 / / rw,relatime shared:1 - xfs /dev/mapper/root rw\n";

// A script first on PATH, named program, that writes one line per run (each argument in
// brackets) to log.
bool fakeProgram(const QString &bin, const QString &program, const QString &log)
{
    const QString script = bin + u'/' + program;
    if (!writeFile(script, "#!/bin/sh\n"
                           "line=''\n"
                           "for a in \"$@\"; do line=\"$line[$a]\"; done\n"
                           "printf '%s\\n' \"$line\" >> '" + QFile::encodeName(log) + "'\n"
                           "exit 0\n"))
        return false;
    return QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

// The XBEL date of a moment, as GLib writes it.
QByteArray stamp(const QDateTime &when)
{
    return when.toUTC().toString(u"yyyy-MM-ddThh:mm:ss.zzz"_s).toUtf8() + "000Z";
}

QByteArray bookmark(const QString &path, const QDateTime &when)
{
    const QByteArray href = QUrl::fromLocalFile(path).toEncoded();
    return "  <bookmark href=\"" + href + "\" added=\"" + stamp(when) + "\" modified=\"" + stamp(when)
           + "\" visited=\"" + stamp(when) + "\">\n"
             "    <info>\n"
             "      <metadata owner=\"http://freedesktop.org\">\n"
             "        <mime:mime-type type=\"text/plain\"/>\n"
             "        <bookmark:applications>\n"
             "          <bookmark:application name=\"Text Editor\" exec=\"&apos;gnome-text-editor %u&apos;\" "
             "modified=\"" + stamp(when) + "\" count=\"2\"/>\n"
             "        </bookmark:applications>\n"
             "      </metadata>\n"
             "    </info>\n"
             "  </bookmark>\n";
}

// What the bookmark of path says: its modified date, and each application entry's name, exec
// and count.
struct Read
{
    QDateTime modified;
    QStringList apps; // "name|exec|count"
    int found = 0;
};

Read readBookmark(const QByteArray &xbel, const QString &path)
{
    Read read;
    QXmlStreamReader reader(xbel);
    bool inside = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.qualifiedName() == u"bookmark"_s) {
            inside = QUrl(reader.attributes().value(u"href"_s).toString()).toLocalFile() == path;
            if (inside) {
                ++read.found;
                // GLib writes microseconds; milliseconds are enough here.
                QString text = reader.attributes().value(u"modified"_s).toString();
                static const QRegularExpression micro(u"^(.{19}\\.\\d{3})\\d*Z$"_s);
                text.replace(micro, u"\\1Z"_s);
                read.modified = QDateTime::fromString(text, Qt::ISODateWithMs);
            }
        } else if (reader.isEndElement() && reader.qualifiedName() == u"bookmark"_s) {
            inside = false;
        } else if (inside && reader.isStartElement() && reader.qualifiedName() == u"bookmark:application"_s) {
            const QXmlStreamAttributes a = reader.attributes();
            read.apps.append(a.value(u"name"_s).toString() + u'|' + a.value(u"exec"_s).toString() + u'|'
                             + a.value(u"count"_s).toString());
        }
    }
    if (reader.hasError())
        read.found = -1;
    return read;
}

} // namespace

class PlacesUiTests : public QObject
{
    Q_OBJECT

private slots:
    void trashViewListsAndRestores();
    void recentFilesFromXbel();
    void networkPlacesConnect();
    void uiTrashAndDrivesInSidebar();
};

void PlacesUiTests::trashViewListsAndRestores()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    const QString mountinfo = scratch.filePath(u"mountinfo"_s);
    QVERIFY(writeFile(mountinfo, rootOnly));
    EnvKeeper keepMounts("RILEY_MOUNTINFO");
    qputenv("RILEY_MOUNTINFO", QFile::encodeName(mountinfo));

    const QString root = ui.root();
    QVERIFY(writeFile(root + u"/a.txt"_s, "alpha"));
    QVERIFY(writeFile(root + u"/dir/b.txt"_s, "beta"));
    const QString files = Riley::Trash::homeTrashPath() + u"/files"_s;
    const QString info = Riley::Trash::homeTrashPath() + u"/info"_s;
    QVERIFY(files.startsWith(ui.scratch()));

    // a.txt and dir/b.txt go to the trash.
    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
    QVERIFY(Riley::Trash::put(root + u"/a.txt"_s));
    QVERIFY(Riley::Trash::put(root + u"/dir/b.txt"_s));
    const QDateTime after = QDateTime::currentDateTime().addSecs(1);

    QVERIFY(ui.start({root}));
    QQuickItem *places = ui.view(u"places"_s);
    QVERIFY(places);
    QTRY_COMPARE(UiHarness::shownNames(places).last(), u"Trash"_s);

    // Choosing Trash in the sidebar: both, with their folders and deletion dates.
    QVERIFY(clickItem(ui, UiHarness::delegateAt(places, places->property("count").toInt() - 1)));
    QTRY_VERIFY(shown(ui, u"trashView"_s));
    QQuickItem *trash = ui.view(u"trash"_s);
    QVERIFY(trash);
    QTRY_COMPARE(UiHarness::shownNames(trash), (QStringList{u"a.txt"_s, u"b.txt"_s}));
    QVERIFY(!shown(ui, u"list"_s));
    QCOMPARE(UiHarness::delegateAt(trash, 0)->property("originalFolder").toString(), root);
    QCOMPARE(UiHarness::delegateAt(trash, 1)->property("originalFolder").toString(), root + u"/dir"_s);
    for (int row = 0; row < 2; ++row) {
        const QString text = UiHarness::delegateAt(trash, row)->property("deletedText").toString();
        const QDateTime when = QDateTime::fromString(text, u"yyyy-MM-dd hh:mm"_s);
        QVERIFY2(when.isValid(), qPrintable(text));
        QVERIFY2(when >= before.addSecs(-60) && when <= after, qPrintable(text));
    }
    QCOMPARE(ui.browser().placeRow(), 0);

    // Restore on b.txt: back in dir, gone from the trash.
    ui.press(Qt::Key_Down);
    QCOMPARE(UiHarness::currentName(trash), u"b.txt"_s);
    QVERIFY(clickItem(ui, ui.view(u"restoreButton"_s)));
    QTRY_COMPARE(readFile(root + u"/dir/b.txt"_s), QByteArray("beta"));
    QTRY_COMPARE(UiHarness::shownNames(trash), (QStringList{u"a.txt"_s}));
    QCOMPARE(namesIn(files), QStringList{u"a.txt"_s});
    QCOMPARE(namesIn(info), QStringList{u"a.txt.trashinfo"_s});

    // a.txt is there again: restoring asks, and Skip leaves both as they are.
    QVERIFY(writeFile(root + u"/a.txt"_s, "new"));
    QTRY_COMPARE(ui.browser().placeRow(), 0);
    QVERIFY(clickItem(ui, ui.view(u"restoreButton"_s)));
    QTRY_VERIFY(ui.browser().ops()->asking());
    ui.browser().ops()->answerClash(FileOps::SkipButton);
    QTRY_VERIFY(!ui.browser().ops()->asking());
    QTRY_VERIFY(!ui.browser().ops()->busy());
    QCOMPARE(readFile(root + u"/a.txt"_s), QByteArray("new"));
    QCOMPARE(namesIn(files), QStringList{u"a.txt"_s});

    // Delete for good asks first; then a.txt leaves files/ and info/.
    QVERIFY(clickItem(ui, ui.view(u"deleteForGoodButton"_s)));
    QTRY_VERIFY(!ui.browser().question().isEmpty());
    QVERIFY(shown(ui, u"question"_s));
    QTest::qWait(200);
    QCOMPARE(namesIn(files), QStringList{u"a.txt"_s});
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(namesIn(files), QStringList());
    QTRY_COMPARE(namesIn(info), QStringList());
    QTRY_COMPARE(trash->property("count").toInt(), 0);
    QCOMPARE(readFile(root + u"/a.txt"_s), QByteArray("new"));

    // Empty Trash asks first; then files/ and info/ are empty.
    QVERIFY(writeFile(root + u"/c.txt"_s, "c"));
    QVERIFY(writeFile(root + u"/d/e.txt"_s, "e"));
    QVERIFY(Riley::Trash::put(root + u"/c.txt"_s));
    QVERIFY(Riley::Trash::put(root + u"/d"_s));
    ui.press(Qt::Key_R, Qt::ControlModifier);
    QTRY_COMPARE(UiHarness::shownNames(trash), (QStringList{u"c.txt"_s, u"d"_s}));
    QVERIFY(clickItem(ui, ui.view(u"emptyTrashButton"_s)));
    QTRY_VERIFY(!ui.browser().question().isEmpty());
    QTest::qWait(200);
    QCOMPARE(namesIn(files).size(), 2);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(namesIn(files), QStringList());
    QTRY_COMPARE(namesIn(info), QStringList());
    QTRY_COMPARE(trash->property("count").toInt(), 0);

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void PlacesUiTests::recentFilesFromXbel()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    const QString root = ui.root();

    // An application for text/plain, the default, that writes the path it got to a marker.
    const QString marker = scratch.filePath(u"marker"_s);
    const QString script = scratch.filePath(u"bin/open.sh"_s);
    QVERIFY(writeFile(script, "#!/bin/sh\nprintf '%s' \"$1\" > '" + marker.toUtf8() + "'\n"));
    QVERIFY(writeFile(scratch.filePath(u"data/applications/rf-test-open.desktop"_s),
                      "[Desktop Entry]\nType=Application\nName=Test Opener\nExec=/bin/sh \"" + script.toUtf8()
                          + "\" %f\nMimeType=text/plain;\n"));
    QVERIFY(writeFile(scratch.filePath(u"config/mimeapps.list"_s),
                      "[Default Applications]\ntext/plain=rf-test-open.desktop\n"));

    // Three bookmarks, modified 3 h, 1 h and 2 h ago; the 2 h one's file is missing.
    const QString three = root + u"/three.txt"_s;
    const QString one = root + u"/one.txt"_s;
    const QString missing = root + u"/missing.txt"_s;
    const QString fresh = root + u"/fresh.txt"_s;
    QVERIFY(writeFile(three, "3"));
    QVERIFY(writeFile(one, "1"));
    QVERIFY(writeFile(fresh, "f"));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray head = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<xbel version=\"1.0\"\n"
                            "      xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\"\n"
                            "      xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\"\n"
                            ">\n";
    const QByteArray bThree = bookmark(three, now.addSecs(-3 * 3600));
    const QByteArray bOne = bookmark(one, now.addSecs(-1 * 3600));
    const QByteArray bMissing = bookmark(missing, now.addSecs(-2 * 3600));
    const QByteArray tail = "</xbel>\n";
    const QByteArray original = head + bThree + bOne + bMissing + tail;
    const QString xbel = scratch.filePath(u"data/recently-used.xbel"_s);
    QVERIFY(writeFile(xbel, original));

    QVERIFY(ui.start({root}));
    QVERIFY(clickItem(ui, ui.view(u"recentPlace"_s)));
    QTRY_VERIFY(shown(ui, u"recentView"_s));
    QQuickItem *recent = ui.view(u"recentFiles"_s);
    QVERIFY(recent);
    QTRY_COMPARE(UiHarness::shownNames(recent), (QStringList{u"one.txt"_s, u"three.txt"_s}));
    QVERIFY(ui.browser().markedRecent());

    // Opening one.txt from there updates its bookmark only.
    QCOMPARE(UiHarness::currentName(recent), u"one.txt"_s);
    const QDateTime beforeOpen = QDateTime::currentDateTimeUtc().addSecs(-1);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(readFile(marker), one.toUtf8());
    const QByteArray updated = readFile(xbel);
    const QDateTime afterOpen = QDateTime::currentDateTimeUtc().addSecs(1);
    const qsizetype oneAt = original.indexOf(bOne);
    QVERIFY(updated.startsWith(original.left(oneAt)));
    QVERIFY(updated.endsWith(original.mid(oneAt + bOne.size())));
    const Read readOne = readBookmark(updated, one);
    QCOMPARE(readOne.found, 1);
    QVERIFY2(readOne.modified >= beforeOpen && readOne.modified <= afterOpen,
             qPrintable(readOne.modified.toString(Qt::ISODateWithMs)));
    QCOMPARE(readOne.apps, (QStringList{u"Text Editor|'gnome-text-editor %u'|2"_s,
                                        u"Riley Files|'riley-files %u'|1"_s}));
    QTRY_COMPARE(UiHarness::shownNames(recent), (QStringList{u"one.txt"_s, u"three.txt"_s}));

    // Opening a file from its folder adds a bookmark; every other one stays byte for byte.
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_VERIFY(shown(ui, u"list"_s));
    QQuickItem *list = ui.view(u"list"_s);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"fresh.txt"_s, u"one.txt"_s, u"three.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"fresh.txt"_s);
    QVERIFY(QFile::remove(marker));
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(readFile(marker), fresh.toUtf8());
    const QByteArray added = readFile(xbel);
    const Read readFresh = readBookmark(added, fresh);
    QCOMPARE(readFresh.found, 1);
    QVERIFY(readFresh.modified >= beforeOpen && readFresh.modified <= QDateTime::currentDateTimeUtc().addSecs(1));
    QCOMPARE(readFresh.apps, QStringList{u"Riley Files|'riley-files %u'|1"_s});
    // Taking the new bookmark out gives back the file as it was before, to the byte.
    static const QRegularExpression freshElement(u"  <bookmark href=\"[^\"]*fresh\\.txt\"[\\s\\S]*?</bookmark>\\n"_s);
    QString without = QString::fromUtf8(added);
    QVERIFY(without.contains(freshElement));
    without.remove(freshElement);
    QCOMPARE(without.toUtf8(), updated);
    QVERIFY(added.contains(bThree) && added.contains(bMissing));

    // The Recent place shows it first now.
    QVERIFY(clickItem(ui, ui.view(u"recentPlace"_s)));
    QTRY_VERIFY(shown(ui, u"recentFiles"_s));
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"recentFiles"_s)),
                 (QStringList{u"fresh.txt"_s, u"one.txt"_s, u"three.txt"_s}));

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void PlacesUiTests::networkPlacesConnect()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    const QString log = scratch.filePath(u"gio.log"_s);
    const QString bin = scratch.filePath(u"bin"_s);
    QVERIFY(QDir().mkpath(bin));
    QVERIFY(fakeProgram(bin, u"gio"_s, log));
    EnvKeeper keepPath("PATH");
    qputenv("PATH", QFile::encodeName(bin) + ":" + qgetenv("PATH"));
    const QString runtime = scratch.filePath(u"run"_s);
    QVERIFY(QDir().mkpath(runtime));
    EnvKeeper keepRuntime("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime));

    QVERIFY(ui.start({ui.root()}));
    QVERIFY(!shown(ui, u"networkHeading"_s));
    QVERIFY(!shown(ui, u"network"_s));

    // Connect to Server, from the File menu's action: the address typed, Return.
    QVERIFY(ui.browser().actions()->trigger(u"connect"_s));
    QTRY_VERIFY(shown(ui, u"connectField"_s));
    ui.type(u"sftp://host/path"_s);
    QCOMPARE(textOf(ui.item(u"connectField"_s)), u"sftp://host/path"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(readFile(log), QByteArray("[mount][sftp://host/path]\n"));
    QVERIFY(!shown(ui, u"connectField"_s));
    QTRY_VERIFY(!ui.browser().network()->connecting());

    // The mount GVfs makes shows in the Network section, by its host's name.
    QVERIFY(QDir().mkpath(runtime + u"/gvfs/sftp:host=host,user=marc"_s));
    QTRY_VERIFY_WITH_TIMEOUT(shown(ui, u"network"_s), 3000);
    QCOMPARE(UiHarness::shownNames(ui.view(u"network"_s)), QStringList{u"host"_s});
    QVERIFY(shown(ui, u"networkHeading"_s));

    // Another kind of address is refused with a message, and gio is not run.
    QVERIFY(ui.browser().actions()->trigger(u"connect"_s));
    QTRY_VERIFY(shown(ui, u"connectField"_s));
    ui.type(u"http://host/x"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY2(textOf(ui.item(u"messageText"_s)).contains(u"cannot be connected to"_s),
                 qPrintable(textOf(ui.item(u"messageText"_s))));
    QTest::qWait(300);
    QCOMPARE(readFile(log), QByteArray("[mount][sftp://host/path]\n"));

    // Shell code in the address is one argument for gio, and nothing runs it.
    QVERIFY(QFile::remove(log));
    QVERIFY(ui.browser().actions()->trigger(u"connect"_s));
    QTRY_VERIFY(shown(ui, u"connectField"_s));
    const QString evil = u"sftp://host/x';touch PWNED;'"_s;
    ui.item(u"connectField"_s)->setProperty("text", evil);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(readFile(log), QByteArray("[mount][") + evil.toUtf8() + "]\n");
    QTRY_VERIFY(!ui.browser().network()->connecting());
    QVERIFY(!QFileInfo::exists(QDir::current().filePath(u"PWNED"_s)));
    QVERIFY(!QFileInfo::exists(QDir::home().filePath(u"PWNED"_s)));
    QCOMPARE(pwnedBelow(ui.scratch()), QStringList());

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

void PlacesUiTests::uiTrashAndDrivesInSidebar()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    const QString mountinfo = scratch.filePath(u"mountinfo"_s);
    QVERIFY(writeFile(mountinfo, rootOnly));
    EnvKeeper keepMounts("RILEY_MOUNTINFO");
    qputenv("RILEY_MOUNTINFO", QFile::encodeName(mountinfo));
    const QString runtime = scratch.filePath(u"run"_s);
    QVERIFY(QDir().mkpath(runtime));
    EnvKeeper keepRuntime("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime));
    QVERIFY(ui.makeFile(u"a.txt"_s, 5));
    QVERIFY(ui.makeFile(u"b.txt"_s, 5));

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s}));

    // Nothing mounted, nothing connected: neither section shows.
    QVERIFY(!shown(ui, u"drivesHeading"_s));
    QVERIFY(!shown(ui, u"drives"_s));
    QVERIFY(!shown(ui, u"networkHeading"_s));
    QVERIFY(!shown(ui, u"network"_s));

    // A drive mounted, a server connected: each section shows with its row.
    QVERIFY(appendFile(mountinfo, "27 22 8:17 / /run/media/marc/USB rw,nosuid shared:6 - vfat /dev/sdb1 rw\n"));
    QVERIFY(QDir().mkpath(runtime + u"/gvfs/sftp:host=box"_s));
    QTRY_VERIFY_WITH_TIMEOUT(shown(ui, u"drives"_s) && shown(ui, u"network"_s), 3000);
    QVERIFY(shown(ui, u"drivesHeading"_s));
    QVERIFY(shown(ui, u"networkHeading"_s));
    QCOMPARE(UiHarness::shownNames(ui.view(u"drives"_s)), QStringList{u"USB"_s});
    QCOMPARE(UiHarness::shownNames(ui.view(u"network"_s)), QStringList{u"box"_s});

    // Choosing Trash shows the trash view, with Restore and Empty Trash.
    QQuickItem *places = ui.view(u"places"_s);
    QVERIFY(places);
    const int trashRow = places->property("count").toInt() - 1;
    QCOMPARE(UiHarness::delegateAt(places, trashRow)->property("name").toString(), u"Trash"_s);
    QVERIFY(clickItem(ui, UiHarness::delegateAt(places, trashRow)));
    QTRY_VERIFY(shown(ui, u"trashView"_s));
    QVERIFY(shown(ui, u"restoreButton"_s));
    QVERIFY(shown(ui, u"emptyTrashButton"_s));
    QVERIFY(!shown(ui, u"list"_s));
    QTRY_COMPARE(ui.browser().markedPlace(), trashRow);

    // Back to the folder.
    ui.press(Qt::Key_Left, Qt::AltModifier);
    QTRY_VERIFY(shown(ui, u"list"_s));
    QVERIFY(!shown(ui, u"trashView"_s));
    QCOMPARE(ui.browser().folder(), ui.root());

    // Shift+Delete on a.txt asks, then deletes it for good: not in the trash.
    list = ui.view(u"list"_s);
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);
    ui.press(Qt::Key_Delete, Qt::ShiftModifier);
    QTRY_VERIFY(!ui.browser().question().isEmpty());
    QVERIFY(shown(ui, u"question"_s));
    QTest::qWait(200);
    QVERIFY(QFileInfo::exists(ui.root() + u"/a.txt"_s));
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(!QFileInfo::exists(ui.root() + u"/a.txt"_s));
    QTRY_COMPARE(UiHarness::shownNames(list), QStringList{u"b.txt"_s});
    QCOMPARE(namesIn(Riley::Trash::homeTrashPath() + u"/files"_s), QStringList());
    QVERIFY(QFileInfo::exists(ui.root() + u"/b.txt"_s));

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

QTEST_MAIN(PlacesUiTests)
#include "placesui_tests.moc"
