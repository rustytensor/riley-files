// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "filejobs.h"
#include "foldermodel.h"
#include "jobqueue.h"
#include "snapshots.h"
#include "trashjobs.h"
#include "undohistory.h"
#include "versionjobs.h"
#include "versionmodel.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <fcntl.h>
#include <sys/stat.h>

using namespace Riley;

class VersionsTests : public QObject
{
    Q_OBJECT

private slots:
    void versionsFoundInSnapshots();
    void versionRestoreNeverLosesCurrent();
    void versionRestoreAsCopy();
    void versionsForFolders();
    void snapshotsAreReadOnly();
    void versionsHiddenWithoutSnapshots();
};

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
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

// The fixture of the milestone: root/.snapshots/1, 2, 3 (taken 2026-11-01, 02, 03 at 10:00 local
// time, described "timeline"), holding doc.txt = "v1", "v2", "v2" (3's copy the same as 2's: same
// size and modified time) and D/a.txt in 1 and 2 only; the live root/doc.txt = "v3" and an empty
// live root/D. The home trash is inside the scratch folder.
struct Fixture
{
    QTemporaryDir tmp;
    QString root, data;

    Fixture()
    {
        root = tmp.filePath(QStringLiteral("root"));
        data = tmp.filePath(QStringLiteral("data"));
        QDir().mkpath(data);
        qputenv("XDG_DATA_HOME", QFile::encodeName(data));
        const QByteArray contents[3] = {"v1", "v2", "v2"};
        const qint64 v1Time = local(1, 9).toSecsSinceEpoch();
        const qint64 v2Time = local(2, 9).toSecsSinceEpoch();
        for (int n = 1; n <= 3; ++n) {
            const QString one = root + QStringLiteral("/.snapshots/%1").arg(n);
            const QString snap = one + QStringLiteral("/snapshot");
            QDir().mkpath(snap);
            const QString utc = local(n).toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            writeFile(one + QStringLiteral("/info.xml"),
                      QStringLiteral("<?xml version=\"1.0\"?>\n<snapshot>\n  <type>single</type>\n  <num>%1</num>\n"
                                     "  <date>%2</date>\n  <description>timeline</description>\n"
                                     "  <cleanup>timeline</cleanup>\n</snapshot>\n")
                          .arg(n)
                          .arg(utc)
                          .toUtf8());
            writeFile(snap + QStringLiteral("/doc.txt"), contents[n - 1]);
            setModified(snap + QStringLiteral("/doc.txt"), n == 1 ? v1Time : v2Time);
            if (n <= 2) {
                QDir().mkpath(snap + QStringLiteral("/D"));
                writeFile(snap + QStringLiteral("/D/a.txt"), "a");
                setModified(snap + QStringLiteral("/D/a.txt"), v1Time);
            }
        }
        QDir().mkpath(root + QStringLiteral("/D"));
        writeFile(root + QStringLiteral("/doc.txt"), "v3");
    }
    QString snap(int n) const { return root + QStringLiteral("/.snapshots/%1/snapshot").arg(n); }
    QString doc() const { return root + QStringLiteral("/doc.txt"); }
    QString trashFiles() const { return data + QStringLiteral("/Trash/files"); }
};

bool runJob(JobQueue &queue, FileJob *job, int msecs = 30000)
{
    QSignalSpy finished(job, &FileJob::finished);
    queue.start(job);
    return finished.wait(msecs);
}

// Every path under folder with its kind, size and modified time to the nanosecond, folders too.
QStringList fingerprint(const QString &folder)
{
    QStringList lines;
    QDirIterator it(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    QStringList paths{folder};
    while (it.hasNext())
        paths.append(it.next());
    for (const QString &path : std::as_const(paths)) {
        struct stat st;
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
            lines.append(path + QStringLiteral(" gone"));
            continue;
        }
        lines.append(QStringLiteral("%1 %2 %3 %4.%5")
                         .arg(path)
                         .arg(st.st_mode, 0, 8)
                         .arg(qint64(st.st_size))
                         .arg(qint64(st.st_mtim.tv_sec))
                         .arg(qint64(st.st_mtim.tv_nsec)));
    }
    lines.sort();
    return lines;
}

QStringList datesOf(const QList<FileVersion> &versions)
{
    QStringList dates;
    for (const FileVersion &v : versions)
        dates.append(VersionModel::dateText(v.snapshot.date));
    return dates;
}

// The one version dated date, or the end of the list.
FileVersion versionOn(const QList<FileVersion> &versions, const QString &date)
{
    for (const FileVersion &v : versions) {
        if (VersionModel::dateText(v.snapshot.date) == date)
            return v;
    }
    return FileVersion();
}

} // namespace

// Versions of root/doc.txt: [2026-11-02 10:00 "v2", 2026-11-01 10:00 "v1"]. 2 and 3 hold the same
// copy and make one version dated by the older; the live "v3" is not listed. A file missing from a
// snapshot has no version there. The model reads them off the main thread.
void VersionsTests::versionsFoundInSnapshots()
{
    Fixture f;
    QCOMPARE(Snapshots::baseFor(f.doc()), f.root);

    const QList<FileVersion> versions = Snapshots::versionsOf(f.doc());
    QCOMPARE(datesOf(versions), (QStringList{QStringLiteral("2026-11-02 10:00"), QStringLiteral("2026-11-01 10:00")}));
    QCOMPARE(readFile(versions.at(0).path), QByteArray("v2"));
    QCOMPARE(readFile(versions.at(1).path), QByteArray("v1"));
    QCOMPARE(versions.at(0).numbers, (QList<int>{2, 3}));
    QCOMPARE(versions.at(0).snapshot.date, local(2));
    QCOMPARE(versions.at(0).snapshot.description, QStringLiteral("timeline"));

    // D/a.txt is in 1 and 2 (the same copy) and not in 3: one version, none dated 2026-11-03.
    const QList<FileVersion> aVersions = Snapshots::versionsOf(f.root + QStringLiteral("/D/a.txt"));
    QCOMPARE(datesOf(aVersions), (QStringList{QStringLiteral("2026-11-01 10:00")}));
    QCOMPARE(aVersions.at(0).numbers, (QList<int>{1, 2}));

    VersionModel model;
    QSignalSpy loaded(&model, &VersionModel::loaded);
    model.setPath(f.doc());
    QVERIFY(loaded.wait(10000));
    QCOMPARE(model.isAvailable(), true);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.dateTextAt(0), QStringLiteral("2026-11-02 10:00"));
    QCOMPARE(model.dateTextAt(1), QStringLiteral("2026-11-01 10:00"));
    QCOMPARE(model.get(0).value(QStringLiteral("description")).toString(), QStringLiteral("timeline"));
    QCOMPARE(readFile(model.copyPathAt(1)), QByteArray("v1"));
    QVERIFY(!model.readingThreads().isEmpty());
    QVERIFY(!model.readingThreads().contains(QThread::currentThreadId()));
}

// Restoring the 2026-11-01 version: doc.txt = "v1", the old "v3" is in the trash; one undo gives
// "v3" again.
void VersionsTests::versionRestoreNeverLosesCurrent()
{
    Fixture f;
    const QStringList before = fingerprint(f.root + QStringLiteral("/.snapshots"));
    const FileVersion v1 = versionOn(Snapshots::versionsOf(f.doc()), QStringLiteral("2026-11-01 10:00"));
    QCOMPARE(readFile(v1.path), QByteArray("v1"));

    JobQueue queue;
    UndoHistory history(&queue);
    auto *job = new VersionRestoreJob(v1.path, f.doc(), v1.snapshot.date, VersionRestoreJob::Mode::Restore);
    QVERIFY(runJob(queue, job));
    QCOMPARE(job->failureCount(), 0);
    QCOMPARE(readFile(f.doc()), QByteArray("v1"));
    QCOMPARE(job->restoredPath(), f.doc());
    const QStringList trashed = QDir(f.trashFiles()).entryList(QDir::Files);
    QCOMPARE(trashed, QStringList{QStringLiteral("doc.txt")});
    QCOMPARE(readFile(f.trashFiles() + QStringLiteral("/doc.txt")), QByteArray("v3"));
    QCOMPARE(history.undoCount(), 1);
    QCOMPARE(history.undoText(), QStringLiteral("Undo Restore Version"));

    FileJob *undo = history.undo();
    QVERIFY(undo);
    QSignalSpy undone(undo, &FileJob::finished);
    QVERIFY(undone.wait(30000));
    QCOMPARE(undo->failureCount(), 0);
    QCOMPARE(readFile(f.doc()), QByteArray("v3"));
    QCOMPARE(fingerprint(f.root + QStringLiteral("/.snapshots")), before);
}

// Restore as Copy of 2026-11-01: "doc (2026-11-01 10.00).txt" = "v1" beside it; doc.txt unchanged.
void VersionsTests::versionRestoreAsCopy()
{
    Fixture f;
    QCOMPARE(Snapshots::copyName(QStringLiteral("doc.txt"), local(1), false), QStringLiteral("doc (2026-11-01 10.00).txt"));
    const FileVersion v1 = versionOn(Snapshots::versionsOf(f.doc()), QStringLiteral("2026-11-01 10:00"));
    JobQueue queue;
    auto *job = new VersionRestoreJob(v1.path, f.doc(), v1.snapshot.date, VersionRestoreJob::Mode::AsCopy);
    QVERIFY(runJob(queue, job));
    QCOMPARE(job->failureCount(), 0);
    const QString copy = f.root + QStringLiteral("/doc (2026-11-01 10.00).txt");
    QCOMPARE(job->restoredPath(), copy);
    QCOMPARE(readFile(copy), QByteArray("v1"));
    QCOMPARE(readFile(f.doc()), QByteArray("v3"));
    QCOMPARE(QDir(f.trashFiles()).entryList(QDir::Files), QStringList());
}

// Versions of root/D: [2026-11-02 10:00, 2026-11-01 10:00]; opening one shows [a.txt], read-only;
// copying a.txt out of it to root works.
void VersionsTests::versionsForFolders()
{
    Fixture f;
    const QList<FileVersion> versions = Snapshots::versionsOf(f.root + QStringLiteral("/D"));
    QCOMPARE(datesOf(versions), (QStringList{QStringLiteral("2026-11-02 10:00"), QStringLiteral("2026-11-01 10:00")}));
    QVERIFY(versions.at(0).isFolder);
    QCOMPARE(versions.at(0).path, f.snap(2) + QStringLiteral("/D"));

    FolderModel model;
    QSignalSpy loaded(&model, &FolderModel::loaded);
    model.setPath(versions.at(1).path);
    QVERIFY(loaded.wait(10000));
    QStringList names;
    for (int row = 0; row < model.count(); ++row)
        names.append(model.nameAt(row));
    QCOMPARE(names, QStringList{QStringLiteral("a.txt")});
    QCOMPARE(model.isReadOnly(), true);
    QCOMPARE(model.property("readOnly").toBool(), true);

    FolderModel live;
    live.setPath(f.root);
    QCOMPARE(live.isReadOnly(), false);

    JobQueue queue;
    auto *copy = new CopyJob({versions.at(1).path + QStringLiteral("/a.txt")}, f.root);
    QVERIFY(runJob(queue, copy));
    QCOMPARE(copy->failureCount(), 0);
    QCOMPARE(readFile(f.root + QStringLiteral("/a.txt")), QByteArray("a"));
}

// Rename, delete, new folder or paste inside a snapshot are refused with "Snapshots can't be
// changed." and nothing under .snapshots changes.
void VersionsTests::snapshotsAreReadOnly()
{
    Fixture f;
    const QString snapshots = f.root + QStringLiteral("/.snapshots");
    const QStringList before = fingerprint(snapshots);
    const QString message = QStringLiteral("Snapshots can't be changed.");
    const QString inside = f.snap(1) + QStringLiteral("/doc.txt");

    JobQueue queue;
    QList<FileJob *> jobs{
        new RenameJob(inside, QStringLiteral("x.txt")),
        new RenameJob(f.snap(1) + QStringLiteral("/D"), QStringLiteral("E")),
        new RenameJob(snapshots, QStringLiteral("gone")),
        new TrashJob({inside}),
        new TrashJob({f.snap(2)}),
        new DeleteJob({f.snap(1) + QStringLiteral("/D/a.txt")}),
        new NewFolderJob(f.snap(1) + QStringLiteral("/D")),
        new NewFolderJob(snapshots),
        new CopyJob({f.doc()}, f.snap(1) + QStringLiteral("/D")),
        new MoveJob({f.doc()}, f.snap(1)),
        new MoveJob({inside}, f.root + QStringLiteral("/D")),
        new VersionRestoreJob(f.snap(2) + QStringLiteral("/doc.txt"), inside, local(2), VersionRestoreJob::Mode::Restore),
        new VersionRestoreJob(f.snap(2) + QStringLiteral("/doc.txt"), inside, local(2), VersionRestoreJob::Mode::AsCopy),
    };
    for (FileJob *job : std::as_const(jobs)) {
        QVERIFY(runJob(queue, job));
        QCOMPARE(job->failureCount(), 1);
        QCOMPARE(job->failures().first().message, message);
        QCOMPARE(job->steps().size(), 0);
    }
    QCOMPARE(fingerprint(snapshots), before);
    QCOMPARE(readFile(f.doc()), QByteArray("v3"));
    QVERIFY(!QFileInfo::exists(f.root + QStringLiteral("/D/doc.txt")));
    QCOMPARE(QDir(f.trashFiles()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList());

    QCOMPARE(Snapshots::isInSnapshot(inside), true);
    QCOMPARE(Snapshots::isInSnapshot(snapshots), true);
    QCOMPARE(Snapshots::isInSnapshot(f.doc()), false);
    QCOMPARE(Snapshots::isInSnapshot(f.root + QStringLiteral("/.snapshotsX/a")), false);
}

// With no .snapshots above a file there is no Previous Versions: no base, nothing available, no
// versions. A .snapshots folder without a numbered snapshot (info.xml and snapshot/) does not count.
void VersionsTests::versionsHiddenWithoutSnapshots()
{
    // /dev/shm is its own file system, so snapshots of the machine's own drives never show above it.
    const QString shm = QStringLiteral("/dev/shm");
    const bool useShm = QFileInfo(shm).isDir() && QFileInfo(shm).isWritable();
    QTemporaryDir tmp(useShm ? shm + QStringLiteral("/riley-versions-XXXXXX") : QDir::tempPath() + QStringLiteral("/riley-versions-XXXXXX"));
    QVERIFY(tmp.isValid());
    const QString plain = tmp.filePath(QStringLiteral("plain.txt"));
    QVERIFY(writeFile(plain, "x"));
    QCOMPARE(Snapshots::baseFor(plain), QString());
    QCOMPARE(Snapshots::versionsOf(plain).size(), 0);

    VersionModel model;
    QSignalSpy loaded(&model, &VersionModel::loaded);
    model.setPath(plain);
    QVERIFY(loaded.wait(10000));
    QCOMPARE(model.isAvailable(), false);
    QCOMPARE(model.count(), 0);

    // A .snapshots folder with a number but no info.xml, and one with info.xml but no snapshot/.
    QDir().mkpath(tmp.filePath(QStringLiteral(".snapshots/1/snapshot")));
    QDir().mkpath(tmp.filePath(QStringLiteral(".snapshots/2")));
    QVERIFY(writeFile(tmp.filePath(QStringLiteral(".snapshots/2/info.xml")),
                      "<snapshot><num>2</num><date>2026-11-01 09:00:00</date></snapshot>"));
    QCOMPARE(Snapshots::baseFor(plain), QString());
    model.reload();
    QVERIFY(loaded.wait(10000));
    QCOMPARE(model.isAvailable(), false);

    // The same file once a real snapshot is there: offered.
    QVERIFY(writeFile(tmp.filePath(QStringLiteral(".snapshots/1/info.xml")),
                      "<snapshot><num>1</num><date>2026-11-01 09:00:00</date></snapshot>"));
    QCOMPARE(Snapshots::baseFor(plain), tmp.path());
    model.reload();
    QVERIFY(loaded.wait(10000));
    QCOMPARE(model.isAvailable(), true);
    QCOMPARE(model.count(), 0); // no copy of plain.txt in it

    // Inside a snapshot there is nothing to offer.
    const qsizetype seen = loaded.count();
    model.setPath(tmp.filePath(QStringLiteral(".snapshots/1/snapshot")));
    QVERIFY(loaded.count() > seen || loaded.wait(10000));
    QCOMPARE(model.isAvailable(), false);
}

QTEST_GUILESS_MAIN(VersionsTests)
#include "versions_tests.moc"
