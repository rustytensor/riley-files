// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Drives in the main window, driven through the real window: the eject mark of a drive in the
// sidebar runs udisksctl (a test one, first on PATH, that writes down every argument it gets)
// with an argument list, says what udisksctl said when it fails, and never lets a shell read a
// mount point. Expected results are the ones written down in the milestone's test list.

#include "browser.h"
#include "uiharness.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
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

// Clicks the eject mark on a row of the sidebar's drives.
bool clickEject(UiHarness &ui, int row)
{
    QQuickItem *drives = ui.view(u"drives"_s);
    QQuickItem *delegate = drives ? UiHarness::delegateAt(drives, row) : nullptr;
    QQuickItem *eject = delegate ? delegate->findChild<QQuickItem *>(u"eject"_s) : nullptr;
    if (!eject || !eject->isVisible())
        return false;
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier,
                      eject->mapToScene(QPointF(eject->width() / 2, eject->height() / 2)).toPoint());
    return true;
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

} // namespace

class DrivesUiTests : public QObject
{
    Q_OBJECT

private slots:
    void drivesEjectWithUdisks();
};

void DrivesUiTests::drivesEjectWithUdisks()
{
    UiHarness ui;
    const QDir scratch(ui.scratch());
    const QString log = scratch.filePath(u"udisksctl.log"_s);
    const QString busy = scratch.filePath(u"busy"_s);

    // Three drives: a USB stick, one whose mount point is shell code, and one whose device name is
    // shell code (a label can give a /dev/disk/by-label name like that). mountinfo writes spaces
    // as \040.
    const QString mountinfo = scratch.filePath(u"mountinfo"_s);
    QVERIFY(writeFile(mountinfo,
                      "22 1 253:1 / / rw,relatime shared:1 - xfs /dev/mapper/root rw\n"
                      "27 22 8:17 / /run/media/marc/USB rw,nosuid,nodev,relatime shared:6 - vfat /dev/sdb1 rw\n"
                      "30 22 8:49 / /run/media/marc/it's;\\040touch\\040PWNED rw,nosuid shared:9 - ext4 /dev/sdd1 rw\n"
                      "31 22 8:65 / /run/media/marc/Zed rw,nosuid shared:10 - ext4 "
                      "/dev/disk/by-label/it's;\\040touch\\040PWNED rw\n"));
    EnvKeeper keepMounts("RILEY_MOUNTINFO");
    qputenv("RILEY_MOUNTINFO", QFile::encodeName(mountinfo));

    // The test udisksctl: one line per run, each argument in brackets; with the busy file
    // there, unmount fails the way udisksctl does.
    const QString bin = scratch.filePath(u"bin"_s);
    QVERIFY(QDir().mkpath(bin));
    const QString script = bin + u"/udisksctl"_s;
    QVERIFY(writeFile(script, "#!/bin/sh\n"
                              "line=''\n"
                              "for a in \"$@\"; do line=\"$line[$a]\"; done\n"
                              "printf '%s\\n' \"$line\" >> '" + QFile::encodeName(log) + "'\n"
                              "if [ \"$1\" = unmount ] && [ -e '" + QFile::encodeName(busy) + "' ]; then\n"
                              "  echo \"Error unmounting $3: target is busy\" >&2\n"
                              "  exit 1\n"
                              "fi\n"
                              "exit 0\n"));
    QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    EnvKeeper keepPath("PATH");
    qputenv("PATH", QFile::encodeName(bin) + ":" + qgetenv("PATH"));

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *drives = ui.view(u"drives"_s);
    QVERIFY(drives);
    QTRY_COMPARE(UiHarness::shownNames(drives), (QStringList{u"it's; touch PWNED"_s, u"USB"_s, u"Zed"_s}));

    // Eject on USB: unmount, then power-off, each an argument list.
    QVERIFY(clickEject(ui, 1));
    QTRY_COMPARE(readFile(log), QByteArray("[unmount][-b][/dev/sdb1]\n[power-off][-b][/dev/sdb1]\n"));
    QTRY_VERIFY(textOf(ui.item(u"messageText"_s)).contains(u"USB"_s));

    // Unmount fails: what udisksctl said is shown, and power-off is not run.
    QVERIFY(writeFile(busy, ""));
    QVERIFY(QFile::remove(log));
    QVERIFY(clickEject(ui, 1));
    QTRY_VERIFY2(textOf(ui.item(u"messageText"_s)).contains(u"target is busy"_s),
                 qPrintable(textOf(ui.item(u"messageText"_s))));
    QTest::qWait(500);
    QCOMPARE(readFile(log), QByteArray("[unmount][-b][/dev/sdb1]\n"));

    // The mount point never reaches a shell: only the device is given.
    QVERIFY(QFile::remove(busy));
    QVERIFY(QFile::remove(log));
    QVERIFY(clickEject(ui, 0));
    QTRY_COMPARE(readFile(log), QByteArray("[unmount][-b][/dev/sdd1]\n[power-off][-b][/dev/sdd1]\n"));
    QCOMPARE(readFile(log).split('\n').first(), QByteArray("[unmount][-b][/dev/sdd1]"));

    // The device name never reaches a shell either: it is one argument, exactly as mountinfo gave it.
    QTRY_VERIFY(textOf(ui.item(u"messageText"_s)).contains(u"it's; touch PWNED"_s));
    QVERIFY(QFile::remove(log));
    QVERIFY(clickEject(ui, 2));
    QTRY_VERIFY(readFile(log).contains("[power-off]"));
    QCOMPARE(readFile(log).split('\n').first(), QByteArray("[unmount][-b][/dev/disk/by-label/it's; touch PWNED]"));

    QVERIFY(!QFileInfo::exists(QDir::current().filePath(u"PWNED"_s)));
    QVERIFY(!QFileInfo::exists(QDir::home().filePath(u"PWNED"_s)));
    QVERIFY(!QFileInfo::exists(u"/run/media/marc/PWNED"_s));
    QCOMPARE(pwnedBelow(ui.scratch()), QStringList());

    QCOMPARE(ui.qmlWarnings(), QStringList());
}

QTEST_MAIN(DrivesUiTests)
#include "drivesui_tests.moc"
