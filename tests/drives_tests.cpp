// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Drives and their trash on real files: the drives read from a mountinfo file, a new mount
// noticed while the program runs, and a file on another file system than the home folder
// trashed to that file system's own trash and put back by undo. Expected results are the ones
// written down in the milestone's test list.

#include "drives.h"
#include "jobqueue.h"
#include "trash.h"
#include "trashjobs.h"
#include "undohistory.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>
#include <optional>

#include <sys/stat.h>
#include <unistd.h>

using namespace Riley;
using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
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

QStringList namesOf(const DrivesModel &model)
{
    QStringList names;
    for (int row = 0; row < model.count(); ++row)
        names.append(model.nameAt(row));
    return names;
}

std::optional<dev_t> deviceOf(const QString &path)
{
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return std::nullopt;
    return st.st_dev;
}

// Starts the job and waits until it has finished.
bool runJob(JobQueue &queue, FileJob *job)
{
    QSignalSpy finished(job, &FileJob::finished);
    queue.start(job);
    return finished.wait(30000);
}

// Keeps an environment variable for the length of a test.
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

class DrivesTests : public QObject
{
    Q_OBJECT

private slots:
    void drivesListUserMounts();
    void trashOnOtherDrives();
};

// System mounts are never drives; a person's mounts are, by name with case ignored; a mount
// that appears while the model runs shows within 3 s.
void DrivesTests::drivesListUserMounts()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString mountinfo = scratch.filePath(u"mountinfo"_s);
    QVERIFY(writeFile(mountinfo,
                      "22 1 253:1 / / rw,relatime shared:1 - xfs /dev/mapper/root rw,seclabel\n"
                      "23 22 253:2 / /home rw,relatime shared:2 - xfs /dev/mapper/home rw,seclabel\n"
                      "24 22 8:1 / /boot rw,relatime shared:3 - ext4 /dev/sda1 rw,seclabel\n"
                      "25 22 0:22 / /proc rw,nosuid,nodev,noexec,relatime shared:4 - proc proc rw\n"
                      "26 22 0:50 / /run/user/1000 rw,nosuid,nodev,relatime shared:5 - tmpfs tmpfs rw,mode=700\n"
                      "27 22 8:17 / /run/media/marc/USB rw,nosuid,nodev,relatime shared:6 - vfat /dev/sdb1 rw,uid=1000\n"
                      "28 22 8:33 / /mnt/data rw,relatime shared:7 - ext4 /dev/sdc1 rw,seclabel\n"));
    EnvKeeper keep("RILEY_MOUNTINFO");
    qputenv("RILEY_MOUNTINFO", QFile::encodeName(mountinfo));

    DrivesModel drives;
    QCOMPARE(namesOf(drives), (QStringList{u"data"_s, u"USB"_s}));
    QCOMPARE(drives.pathAt(0), u"/mnt/data"_s);
    QCOMPARE(drives.pathAt(1), u"/run/media/marc/USB"_s);
    QCOMPARE(drives.drives().at(1).device, u"/dev/sdb1"_s);
    QCOMPARE(drives.drives().at(1).fsType, u"vfat"_s);

    // A stick plugged in while the program runs.
    QVERIFY(appendFile(mountinfo, "29 22 8:49 / /media/Backup rw,relatime shared:8 - ext4 /dev/sdd1 rw\n"));
    QTRY_COMPARE_WITH_TIMEOUT(namesOf(drives), (QStringList{u"Backup"_s, u"data"_s, u"USB"_s}), 3000);
}

// A file on another file system than the home folder goes to that file system's own trash,
// <top>/.Trash-<uid>, its info file naming its path relative to <top>; undo puts it back.
void DrivesTests::trashOnOtherDrives()
{
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    EnvKeeper keep("XDG_DATA_HOME");
    const QString data = scratch.filePath(u"data"_s);
    QVERIFY(QDir().mkpath(data));
    qputenv("XDG_DATA_HOME", QFile::encodeName(data));
    const std::optional<dev_t> homeDevice = deviceOf(data);
    QVERIFY(homeDevice);

    // A writable folder on another file system, whose top folder can hold a trash.
    const QString uid = QString::number(::getuid());
    QStringList tried;
    std::unique_ptr<QTemporaryDir> place;
    QString top;
    const QStringList candidates{u"/dev/shm"_s, qEnvironmentVariable("XDG_RUNTIME_DIR"), u"/run/user/"_s + uid,
                                 u"/tmp"_s, u"/var/tmp"_s, QDir::homePath()};
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty() || tried.contains(candidate))
            continue;
        tried.append(candidate);
        const std::optional<dev_t> device = deviceOf(candidate);
        if (!device || *device == *homeDevice || !QFileInfo(candidate).isWritable())
            continue;
        auto made = std::make_unique<QTemporaryDir>(candidate + u"/riley-drive-XXXXXX"_s);
        if (!made->isValid())
            continue;
        const QString itsTop = Trash::topDirOf(made->path());
        const QString trash = Trash::driveTrashPath(itsTop);
        if (!QFileInfo(trash).isDir() && !QFileInfo(itsTop).isWritable())
            continue;
        place = std::move(made);
        top = itsTop;
        break;
    }
    QVERIFY2(place, qPrintable(u"No second file system is reachable here: tried "_s + tried.join(u", "_s)
                               + u" (none is writable, on another file system than "_s + data + u")"_s));
    qInfo("Another file system: %s, mounted on %s", qPrintable(place->path()), qPrintable(top));

    const QString trash = top + (top == u"/"_s ? u""_s : u"/"_s) + u".Trash-"_s + uid;
    QCOMPARE(Trash::driveTrashPath(top), trash);
    const bool trashWasThere = QFileInfo(trash).exists();
    const auto cleanup = qScopeGuard([&] {
        if (!trashWasThere)
            Trash::removeForGood(trash);
    });

    // A unique name, so nothing left in that trash before can take it.
    const QString name = u"my file "_s + QFileInfo(place->path()).fileName() + u".txt"_s;
    const QString original = place->path() + u"/"_s + name;
    QVERIFY(writeFile(original, "on another drive\n"));
    QCOMPARE(Trash::trashPathFor(original), trash);

    JobQueue queue;
    UndoHistory history(&queue);
    auto *job = new TrashJob({original});
    QVERIFY(runJob(queue, job));
    QCOMPARE(job->failureCount(), 0);
    QVERIFY(!QFileInfo::exists(original));

    const QString trashed = trash + u"/files/"_s + name;
    const QString infoPath = trash + u"/info/"_s + name + u".trashinfo"_s;
    QCOMPARE(job->trashedPaths(), QStringList{trashed});
    QCOMPARE(readFile(trashed), QByteArray("on another drive\n"));
    // Nothing went to the home trash.
    QVERIFY(!QFileInfo::exists(data + u"/Trash/files/"_s + name));

    // The info file: the path below the top folder, percent-encoded, never absolute.
    const QString relative = original.mid(top == u"/"_s ? 1 : top.size() + 1);
    QVERIFY(!relative.startsWith(u'/'));
    const QByteArray info = readFile(infoPath);
    const QList<QByteArray> lines = info.split('\n');
    QVERIFY2(lines.size() >= 3, info.constData());
    QCOMPARE(lines.at(0), QByteArray("[Trash Info]"));
    QCOMPARE(lines.at(1), "Path=" + QUrl::toPercentEncoding(relative, "/"));
    QVERIFY(lines.at(1).contains("my%20file%20"));
    QVERIFY(lines.at(2).startsWith("DeletionDate="));
    QVERIFY(QDateTime::fromString(QString::fromLatin1(lines.at(2).mid(13)), u"yyyy-MM-ddThh:mm:ss"_s).isValid());

    const std::optional<TrashEntry> entry = Trash::entryFor(trashed);
    QVERIFY(entry);
    QCOMPARE(entry->originalPath, original);
    QVERIFY(Trash::isInTrash(trashed));
    QVERIFY(Trash::trashFolders({top}).contains(trash));

    // Undo puts it back, and its trash entry goes.
    FileJob *undo = history.undo();
    QVERIFY(undo);
    QSignalSpy undone(undo, &FileJob::finished);
    QVERIFY(undone.wait(30000));
    QCOMPARE(undo->failureCount(), 0);
    QCOMPARE(readFile(original), QByteArray("on another drive\n"));
    QVERIFY(!QFileInfo::exists(trashed));
    QVERIFY(!QFileInfo::exists(infoPath));
}

QTEST_GUILESS_MAIN(DrivesTests)
#include "drives_tests.moc"
