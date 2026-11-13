// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "filejobs.h"
#include "jobqueue.h"
#include "trash.h"
#include "trashjobs.h"
#include "undohistory.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <atomic>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Riley;

class FileOpsTests : public QObject
{
    Q_OBJECT

private slots:
    void copyKeepsWhatItCopies();
    void clashesAreAskedNeverOverwritten();
    void copyIntoItselfIsRefused();
    void moveFallsBackToCopyAcrossDrives();
    void trashFollowsTheFreedesktopSpec();
    void undoPutsThingsBack();
    void stopEndsACopyCleanly();
    void copyReportsWhatFailed();
    void progressIsReported();
    void mergeFoldersAsksPerFile();
};

namespace {

// A scratch area: src/ and dst/ folders, and the home trash inside it (XDG_DATA_HOME), so
// nothing outside the temporary folder is touched.
struct Scratch
{
    QTemporaryDir tmp;
    QString src, dst, data;

    Scratch()
    {
        src = tmp.filePath(QStringLiteral("src"));
        dst = tmp.filePath(QStringLiteral("dst"));
        data = tmp.filePath(QStringLiteral("data"));
        QDir().mkpath(src);
        QDir().mkpath(dst);
        QDir().mkpath(data);
        qputenv("XDG_DATA_HOME", QFile::encodeName(data));
    }
    QString trashFiles() const { return data + QStringLiteral("/Trash/files"); }
};

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

// Every name in a folder, hidden ones too, sorted.
QStringList namesIn(const QString &folder)
{
    QStringList names = QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                               QDir::Name);
    return names;
}

// Runs an undo (or redo) of the history and waits for it; the job, or nullptr when there was
// nothing to undo or it did not finish.
FileJob *runUndo(UndoHistory &history, bool redo = false)
{
    FileJob *job = redo ? history.redo() : history.undo();
    if (!job)
        return nullptr;
    QSignalSpy finished(job, &FileJob::finished);
    return finished.wait(30000) ? job : nullptr;
}

// Starts the job and waits until it has finished.
bool runJob(JobQueue &queue, FileJob *job, int msecs = 30000)
{
    QSignalSpy finished(job, &FileJob::finished);
    queue.start(job);
    return finished.wait(msecs);
}

} // namespace

// Bytes, mode and modified time are kept; a symlink is copied as a symlink with the same target.
void FileOpsTests::copyKeepsWhatItCopies()
{
    Scratch s;
    const QString a = s.src + QStringLiteral("/a.txt");
    QVERIFY(writeFile(a, "alpha"));
    QVERIFY(::chmod(QFile::encodeName(a).constData(), 0640) == 0);
    const qint64 when = QDateTime(QDate(2020, 1, 1), QTime(12, 0), QTimeZone::UTC).toSecsSinceEpoch();
    QCOMPARE(when, qint64(1577880000));
    const struct timespec times[2] = {{when, 0}, {when, 0}};
    QVERIFY(::utimensat(AT_FDCWD, QFile::encodeName(a).constData(), times, 0) == 0);
    QVERIFY(QDir(s.src).mkdir(QStringLiteral("D")));
    QVERIFY(writeFile(s.src + QStringLiteral("/D/b.txt"), "beta"));
    QVERIFY(::symlink("b.txt", QFile::encodeName(s.src + QStringLiteral("/D/l")).constData()) == 0);

    JobQueue queue;
    auto *job = new CopyJob({a, s.src + QStringLiteral("/D")}, s.dst);
    QVERIFY(runJob(queue, job));
    QCOMPARE(job->state(), FileJob::State::Done);
    QCOMPARE(job->failureCount(), 0);

    QCOMPARE(namesIn(s.dst), (QStringList{QStringLiteral("D"), QStringLiteral("a.txt")}));
    const QString ca = s.dst + QStringLiteral("/a.txt");
    QCOMPARE(readFile(ca), QByteArray("alpha"));
    struct stat st;
    QVERIFY(::stat(QFile::encodeName(ca).constData(), &st) == 0);
    QCOMPARE(int(st.st_mode & 07777), 0640);
    QCOMPARE(qint64(st.st_mtim.tv_sec), when);

    QCOMPARE(namesIn(s.dst + QStringLiteral("/D")), (QStringList{QStringLiteral("b.txt"), QStringLiteral("l")}));
    QCOMPARE(readFile(s.dst + QStringLiteral("/D/b.txt")), QByteArray("beta"));
    const QByteArray link = QFile::encodeName(s.dst + QStringLiteral("/D/l"));
    QVERIFY(::lstat(link.constData(), &st) == 0);
    QVERIFY(S_ISLNK(st.st_mode));
    char target[64] = {};
    QCOMPARE(::readlink(link.constData(), target, sizeof target - 1), ssize_t(5));
    QCOMPARE(QByteArray(target), QByteArray("b.txt"));
}

// A taken name stops the job and asks. Skip keeps the old, Keep both adds "a (2).txt",
// Replace sends the old one to the trash first; "for all" answers every later clash.
void FileOpsTests::clashesAreAskedNeverOverwritten()
{
    const FileJob::ClashChoice choices[] = {FileJob::ClashChoice::Skip, FileJob::ClashChoice::KeepBoth,
                                            FileJob::ClashChoice::Replace};
    for (const FileJob::ClashChoice choice : choices) {
        Scratch s;
        QVERIFY(writeFile(s.dst + QStringLiteral("/a.txt"), "old"));
        QVERIFY(writeFile(s.src + QStringLiteral("/a.txt"), "new"));

        JobQueue queue;
        auto *job = new CopyJob({s.src + QStringLiteral("/a.txt")}, s.dst);
        int asked = 0;
        bool waited = false;
        connect(job, &FileJob::clashAsked, this, [&](const QString &source, const QString &target) {
            ++asked;
            QCOMPARE(source, s.src + QStringLiteral("/a.txt"));
            QCOMPARE(target, s.dst + QStringLiteral("/a.txt"));
            waited = job->state() == FileJob::State::Asking && readFile(target) == "old";
            job->answerClash(choice);
        });
        QVERIFY(runJob(queue, job));
        QCOMPARE(asked, 1);
        QVERIFY(waited);
        QCOMPARE(job->failureCount(), 0);

        switch (choice) {
        case FileJob::ClashChoice::Skip:
            QCOMPARE(namesIn(s.dst), QStringList{QStringLiteral("a.txt")});
            QCOMPARE(readFile(s.dst + QStringLiteral("/a.txt")), QByteArray("old"));
            break;
        case FileJob::ClashChoice::KeepBoth:
            QCOMPARE(namesIn(s.dst), (QStringList{QStringLiteral("a (2).txt"), QStringLiteral("a.txt")}));
            QCOMPARE(readFile(s.dst + QStringLiteral("/a.txt")), QByteArray("old"));
            QCOMPARE(readFile(s.dst + QStringLiteral("/a (2).txt")), QByteArray("new"));
            QCOMPARE(job->targets(), QStringList{s.dst + QStringLiteral("/a (2).txt")});
            break;
        case FileJob::ClashChoice::Replace:
            QCOMPARE(namesIn(s.dst), QStringList{QStringLiteral("a.txt")});
            QCOMPARE(readFile(s.dst + QStringLiteral("/a.txt")), QByteArray("new"));
            QCOMPARE(namesIn(s.trashFiles()), QStringList{QStringLiteral("a.txt")});
            QCOMPARE(readFile(s.trashFiles() + QStringLiteral("/a.txt")), QByteArray("old"));
            QVERIFY(QFileInfo::exists(s.data + QStringLiteral("/Trash/info/a.txt.trashinfo")));
            break;
        case FileJob::ClashChoice::Merge: // files: never offered
            break;
        }
    }

    // "For all": two clashes, one question.
    Scratch s;
    for (const char *name : {"a.txt", "b.txt"}) {
        QVERIFY(writeFile(s.dst + QLatin1Char('/') + QLatin1String(name), "old"));
        QVERIFY(writeFile(s.src + QLatin1Char('/') + QLatin1String(name), "new"));
    }
    JobQueue queue;
    auto *job = new CopyJob({s.src + QStringLiteral("/a.txt"), s.src + QStringLiteral("/b.txt")}, s.dst);
    int asked = 0;
    connect(job, &FileJob::clashAsked, this, [&] {
        ++asked;
        job->answerClash(FileJob::ClashChoice::KeepBoth, true);
    });
    QVERIFY(runJob(queue, job));
    QCOMPARE(asked, 1);
    QCOMPARE(namesIn(s.dst), (QStringList{QStringLiteral("a (2).txt"), QStringLiteral("a.txt"),
                                          QStringLiteral("b (2).txt"), QStringLiteral("b.txt")}));
    QCOMPARE(readFile(s.dst + QStringLiteral("/b.txt")), QByteArray("old"));
    QCOMPARE(readFile(s.dst + QStringLiteral("/b (2).txt")), QByteArray("new"));
}

// D into D/sub: refused with an error naming D, and nothing is made anywhere.
void FileOpsTests::copyIntoItselfIsRefused()
{
    Scratch s;
    const QString d = s.src + QStringLiteral("/D");
    QVERIFY(QDir().mkpath(d + QStringLiteral("/sub")));
    QVERIFY(writeFile(d + QStringLiteral("/f.txt"), "f"));

    JobQueue queue;
    auto *copy = new CopyJob({d}, d + QStringLiteral("/sub"));
    QVERIFY(runJob(queue, copy));
    auto *move = new MoveJob({d}, d + QStringLiteral("/sub"));
    QVERIFY(runJob(queue, move));

    for (FileJob *job : {static_cast<FileJob *>(copy), static_cast<FileJob *>(move)}) {
        const QList<JobFailure> failures = job->failures();
        QCOMPARE(failures.size(), 1);
        QCOMPARE(failures.first().path, d);
        QVERIFY2(failures.first().message.contains(QStringLiteral("“D”")), qPrintable(failures.first().message));
        QVERIFY(job->steps().isEmpty());
    }
    QCOMPARE(namesIn(s.src), QStringList{QStringLiteral("D")});
    QCOMPARE(namesIn(d), (QStringList{QStringLiteral("f.txt"), QStringLiteral("sub")}));
    QCOMPARE(namesIn(d + QStringLiteral("/sub")), QStringList());
    QCOMPARE(namesIn(s.dst), QStringList());
}

// When rename says "another drive", the move copies everything and only then removes the
// source. A copy that fails half-way leaves the source as it was and no copy behind.
void FileOpsTests::moveFallsBackToCopyAcrossDrives()
{
    {
        Scratch s;
        QVERIFY(QDir().mkpath(s.src + QStringLiteral("/S/y")));
        QVERIFY(writeFile(s.src + QStringLiteral("/S/x.txt"), "x"));
        QVERIFY(writeFile(s.src + QStringLiteral("/S/y/z.txt"), "z"));
        QVERIFY(writeFile(s.src + QStringLiteral("/f.txt"), "f"));

        JobQueue queue;
        auto *job = new MoveJob({s.src + QStringLiteral("/S"), s.src + QStringLiteral("/f.txt")}, s.dst);
        job->setPretendOtherDrive(true);
        QVERIFY(runJob(queue, job));
        QCOMPARE(job->failureCount(), 0);
        QCOMPARE(namesIn(s.src), QStringList());
        QCOMPARE(namesIn(s.dst), (QStringList{QStringLiteral("S"), QStringLiteral("f.txt")}));
        QCOMPARE(namesIn(s.dst + QStringLiteral("/S")), (QStringList{QStringLiteral("x.txt"), QStringLiteral("y")}));
        QCOMPARE(readFile(s.dst + QStringLiteral("/S/x.txt")), QByteArray("x"));
        QCOMPARE(readFile(s.dst + QStringLiteral("/S/y/z.txt")), QByteArray("z"));
        QCOMPARE(readFile(s.dst + QStringLiteral("/f.txt")), QByteArray("f"));
        const QList<JobStep> steps = job->steps();
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps.at(0).kind, JobStep::Moved);
        QCOMPARE(steps.at(0).from, s.src + QStringLiteral("/S"));
        QCOMPARE(steps.at(0).to, s.dst + QStringLiteral("/S"));
    }
    {
        Scratch s;
        QVERIFY(QDir().mkpath(s.src + QStringLiteral("/S")));
        QVERIFY(writeFile(s.src + QStringLiteral("/S/a.txt"), "a"));
        QVERIFY(writeFile(s.src + QStringLiteral("/S/b.txt"), "b"));
        QVERIFY(writeFile(s.src + QStringLiteral("/S/c.txt"), "c"));
        const QByteArray b = QFile::encodeName(s.src + QStringLiteral("/S/b.txt"));
        QVERIFY(::chmod(b.constData(), 0) == 0);

        JobQueue queue;
        auto *job = new MoveJob({s.src + QStringLiteral("/S")}, s.dst);
        job->setPretendOtherDrive(true);
        QVERIFY(runJob(queue, job));
        ::chmod(b.constData(), 0644);
        QVERIFY(job->failureCount() >= 1);
        QVERIFY(job->steps().isEmpty());
        QCOMPARE(namesIn(s.dst), QStringList());
        QCOMPARE(namesIn(s.src + QStringLiteral("/S")),
                 (QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));
        QCOMPARE(readFile(s.src + QStringLiteral("/S/a.txt")), QByteArray("a"));
        QCOMPARE(readFile(s.src + QStringLiteral("/S/b.txt")), QByteArray("b"));
        QCOMPARE(readFile(s.src + QStringLiteral("/S/c.txt")), QByteArray("c"));
    }
}

// $HOME/f.txt goes to Trash/files/f.txt with Trash/info/f.txt.trashinfo: "[Trash Info]", the
// original path percent-encoded, and the local deletion time. A second f.txt is "f (2).txt".
void FileOpsTests::trashFollowsTheFreedesktopSpec()
{
    // A home whose path needs encoding: a space and an "é" (two UTF-8 bytes).
    QTemporaryDir home(QDir::tempPath() + QStringLiteral("/home é x-XXXXXX"));
    QVERIFY(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(home.path()));
    const QString data = home.filePath(QStringLiteral("data"));
    qputenv("XDG_DATA_HOME", QFile::encodeName(data));
    struct PutBack { QByteArray home; ~PutBack() { qputenv("HOME", home); } } putBack{oldHome};

    const QString homePath = QDir::homePath();
    QCOMPARE(homePath, home.path());
    QCOMPARE(Trash::homeTrashPath(), data + QStringLiteral("/Trash"));
    const QString f = homePath + QStringLiteral("/f.txt");

    // The Path= value worked out by hand: every byte outside A-Z a-z 0-9 - . _ ~ / as %XX.
    QByteArray expectedPath;
    for (const char c : f.toUtf8()) {
        const uchar u = uchar(c);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '-' || c == '.'
            || c == '_' || c == '~' || c == '/')
            expectedPath += c;
        else
            expectedPath += '%' + QByteArray::number(u, 16).toUpper().rightJustified(2, '0');
    }
    QVERIFY2(expectedPath.contains("/home%20%C3%A9%20x-"), expectedPath.constData());
    QVERIFY2(expectedPath.endsWith("/f.txt"), expectedPath.constData());

    const QString files = data + QStringLiteral("/Trash/files");
    const QString info = data + QStringLiteral("/Trash/info");
    static const QRegularExpression dateLine(QStringLiteral("^DeletionDate=(\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d)$"));

    JobQueue queue;
    const QByteArray contents[] = {"one", "two"};
    const QString names[] = {QStringLiteral("f.txt"), QStringLiteral("f (2).txt")};
    for (int i = 0; i < 2; ++i) {
        QVERIFY(writeFile(f, contents[i]));
        const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
        auto *job = new TrashJob({f});
        QVERIFY(runJob(queue, job));
        const QDateTime after = QDateTime::currentDateTime().addSecs(1);
        QVERIFY2(job->failureCount() == 0, qPrintable(job->failures().value(0).message));
        QVERIFY(!QFileInfo::exists(f));
        QCOMPARE(job->trashedPaths(), QStringList{files + QLatin1Char('/') + names[i]});
        QCOMPARE(job->steps().size(), 1);
        QCOMPARE(job->steps().first().kind, JobStep::Trashed);
        QCOMPARE(job->steps().first().from, f);

        QCOMPARE(readFile(files + QLatin1Char('/') + names[i]), contents[i]);
        const QList<QByteArray> lines = readFile(info + QLatin1Char('/') + names[i] + QStringLiteral(".trashinfo"))
                                            .split('\n');
        QVERIFY2(lines.size() >= 3, qPrintable(QString::number(lines.size())));
        QCOMPARE(lines.at(0), QByteArray("[Trash Info]"));
        QCOMPARE(lines.at(1), QByteArray("Path=") + expectedPath);
        const QRegularExpressionMatch m = dateLine.match(QString::fromLatin1(lines.at(2)));
        QVERIFY2(m.hasMatch(), lines.at(2).constData());
        const QDateTime when = QDateTime::fromString(m.captured(1), QStringLiteral("yyyy-MM-ddThh:mm:ss"));
        QVERIFY(when.isValid());
        QVERIFY2(when >= before.addMSecs(-before.time().msec()) && when <= after,
                 qPrintable(m.captured(1) + QLatin1Char(' ') + before.toString(Qt::ISODate)));
    }
    QCOMPARE(namesIn(files), (QStringList{QStringLiteral("f (2).txt"), QStringLiteral("f.txt")}));
    QCOMPARE(namesIn(info), (QStringList{QStringLiteral("f (2).txt.trashinfo"), QStringLiteral("f.txt.trashinfo")}));

    // Each info file leads back to the same place; restoring "f (2).txt" puts "two" back as
    // f.txt and drops its info file; deleting f.txt for good empties the trash.
    const std::optional<TrashEntry> entry = Trash::entryFor(files + QStringLiteral("/f (2).txt"));
    QVERIFY(entry);
    QCOMPARE(entry->originalPath, f);
    auto *restore = new RestoreJob({files + QStringLiteral("/f (2).txt")});
    QVERIFY(runJob(queue, restore));
    QCOMPARE(restore->failureCount(), 0);
    QCOMPARE(readFile(f), QByteArray("two"));
    QCOMPARE(namesIn(files), QStringList{QStringLiteral("f.txt")});
    QCOMPARE(namesIn(info), QStringList{QStringLiteral("f.txt.trashinfo")});
    auto *again = new RestoreJob({files + QStringLiteral("/f.txt")}); // f.txt is taken now
    QVERIFY(runJob(queue, again));
    QCOMPARE(again->failureCount(), 1);
    QCOMPARE(readFile(f), QByteArray("two"));
    QCOMPARE(readFile(files + QStringLiteral("/f.txt")), QByteArray("one"));
    auto *erase = new DeleteJob({files + QStringLiteral("/f.txt")});
    QVERIFY(runJob(queue, erase));
    QCOMPARE(erase->failureCount(), 0);
    QCOMPARE(namesIn(files), QStringList());
    QCOMPARE(namesIn(info), QStringList());
    QCOMPARE(readFile(f), QByteArray("two"));
}

// Each kind of job undone in turn, the listing checked after each; redo after an undo; and an
// undo refused, changing nothing, when the file was renamed away since.
void FileOpsTests::undoPutsThingsBack()
{
    Scratch s;
    const QString top = s.src;
    const QString a = top + QStringLiteral("/a.txt");
    const QString d = top + QStringLiteral("/D");
    QVERIFY(writeFile(a, "alpha"));
    QVERIFY(QDir(top).mkdir(QStringLiteral("D")));
    const QStringList start{QStringLiteral("D"), QStringLiteral("a.txt")};

    JobQueue queue;
    UndoHistory history(&queue);
    QVERIFY(!history.canUndo());
    QVERIFY(!runUndo(history));

    // New folder N, undone.
    QVERIFY(runJob(queue, new NewFolderJob(top, QStringLiteral("N"))));
    QCOMPARE(namesIn(top), (QStringList{QStringLiteral("D"), QStringLiteral("N"), QStringLiteral("a.txt")}));
    QCOMPARE(history.undoText(), QStringLiteral("Undo New Folder"));
    FileJob *undo = runUndo(history);
    QVERIFY(undo);
    QCOMPARE(undo->failureCount(), 0);
    QCOMPARE(namesIn(top), start);

    // Rename a.txt -> b.txt, undone, redone, undone again.
    QVERIFY(runJob(queue, new RenameJob(a, QStringLiteral("b.txt"))));
    QCOMPARE(namesIn(top), (QStringList{QStringLiteral("D"), QStringLiteral("b.txt")}));
    QCOMPARE(history.undoText(), QStringLiteral("Undo Rename"));
    QVERIFY(runUndo(history));
    QCOMPARE(namesIn(top), start);
    QCOMPARE(readFile(a), QByteArray("alpha"));
    QVERIFY(history.canRedo());
    QCOMPARE(history.redoText(), QStringLiteral("Redo Rename"));
    QVERIFY(runUndo(history, true));
    QCOMPARE(namesIn(top), (QStringList{QStringLiteral("D"), QStringLiteral("b.txt")}));
    QVERIFY(!history.canRedo());
    QVERIFY(runUndo(history));
    QCOMPARE(namesIn(top), start);

    // Move a.txt into D, undone.
    QVERIFY(runJob(queue, new MoveJob({a}, d)));
    QCOMPARE(namesIn(top), QStringList{QStringLiteral("D")});
    QCOMPARE(namesIn(d), QStringList{QStringLiteral("a.txt")});
    QVERIFY(runUndo(history));
    QCOMPARE(namesIn(top), start);
    QCOMPARE(namesIn(d), QStringList());
    QCOMPARE(readFile(a), QByteArray("alpha"));

    // Copy a.txt to D, undone: the copy goes to the trash (where N already is), a.txt stays.
    QVERIFY(runJob(queue, new CopyJob({a}, d)));
    QCOMPARE(namesIn(d), QStringList{QStringLiteral("a.txt")});
    QVERIFY(runUndo(history));
    QCOMPARE(namesIn(d), QStringList());
    QCOMPARE(namesIn(top), start);
    QCOMPARE(namesIn(s.trashFiles()), (QStringList{QStringLiteral("N"), QStringLiteral("a.txt")}));
    QCOMPARE(readFile(s.trashFiles() + QStringLiteral("/a.txt")), QByteArray("alpha"));

    // Trash a.txt (it becomes "a (2).txt" in the trash), undone: a.txt is back, same bytes, and
    // its trash entry is gone.
    const QString info = s.data + QStringLiteral("/Trash/info");
    QVERIFY(runJob(queue, new TrashJob({a})));
    QCOMPARE(namesIn(top), QStringList{QStringLiteral("D")});
    QCOMPARE(namesIn(s.trashFiles()),
             (QStringList{QStringLiteral("N"), QStringLiteral("a (2).txt"), QStringLiteral("a.txt")}));
    QVERIFY(runUndo(history));
    QCOMPARE(namesIn(top), start);
    QCOMPARE(readFile(a), QByteArray("alpha"));
    QCOMPARE(namesIn(s.trashFiles()), (QStringList{QStringLiteral("N"), QStringLiteral("a.txt")}));
    QCOMPARE(namesIn(info), (QStringList{QStringLiteral("N.trashinfo"), QStringLiteral("a.txt.trashinfo")}));

    // Rename a.txt -> b.txt, then b.txt renamed away to c.txt behind the history's back: the
    // undo is refused with a message naming b.txt, and nothing changes.
    QVERIFY(runJob(queue, new RenameJob(a, QStringLiteral("b.txt"))));
    QVERIFY(::rename(QFile::encodeName(top + QStringLiteral("/b.txt")).constData(),
                     QFile::encodeName(top + QStringLiteral("/c.txt")).constData()) == 0);
    QSignalSpy refused(&history, &UndoHistory::refused);
    undo = runUndo(history);
    QVERIFY(undo);
    const QList<JobFailure> failures = undo->failures();
    QCOMPARE(failures.size(), 1);
    QVERIFY2(failures.first().message.contains(QStringLiteral("“b.txt”")), qPrintable(failures.first().message));
    QCOMPARE(refused.size(), 1);
    QCOMPARE(refused.first().first().toString(), failures.first().message);
    QVERIFY(undo->steps().isEmpty());
    QCOMPARE(namesIn(top), (QStringList{QStringLiteral("D"), QStringLiteral("c.txt")}));
    QCOMPARE(readFile(top + QStringLiteral("/c.txt")), QByteArray("alpha"));
    QCOMPARE(namesIn(d), QStringList());
    QCOMPARE(namesIn(s.trashFiles()), (QStringList{QStringLiteral("N"), QStringLiteral("a.txt")}));
}

// 5,000 files of 4 KiB, stopped once 100 are done: the job ends as stopped within 1 s, and
// every file in the destination is whole.
void FileOpsTests::stopEndsACopyCleanly()
{
    Scratch s;
    const QString f = s.src + QStringLiteral("/F");
    QVERIFY(QDir().mkpath(f));
    for (int i = 0; i < 5000; ++i) {
        const QByteArray line = QByteArray::number(i).rightJustified(8, '0');
        QVERIFY(writeFile(f + QStringLiteral("/f%1").arg(i, 4, 10, QLatin1Char('0')), line.repeated(4096 / 8)));
    }

    JobQueue queue;
    UndoHistory history(&queue);
    auto *job = new CopyJob({f}, s.dst);
    QSignalSpy finished(job, &FileJob::finished);
    std::atomic<bool> giveUp{false};
    std::atomic<qint64> stoppedAt{-1};
    QElapsedTimer clock;
    clock.start();
    // Watches from another thread, so the stop comes right at 100 files and not whenever the
    // next throttled progress signal arrives.
    std::thread stopper([&] {
        while (!giveUp.load() && job->filesDone() < 100)
            std::this_thread::yield();
        job->stop();
        stoppedAt.store(clock.elapsed());
    });
    queue.start(job);
    const bool ended = finished.wait(30000);
    const qint64 endedAt = clock.elapsed();
    giveUp.store(true);
    stopper.join();
    QVERIFY(ended);
    QCOMPARE(job->state(), FileJob::State::Stopped);
    QVERIFY2(endedAt - stoppedAt.load() < 1000, qPrintable(QString::number(endedAt - stoppedAt.load())));
    QVERIFY2(job->filesDone() < 5000, "the copy was not stopped");

    const QStringList copied = namesIn(s.dst + QStringLiteral("/F"));
    QVERIFY(copied.size() >= 100);
    QVERIFY(copied.size() < 5000);
    for (const QString &name : copied) {
        QVERIFY2(!name.startsWith(QLatin1Char('.')), qPrintable(name));
        QCOMPARE(readFile(s.dst + QStringLiteral("/F/") + name), readFile(f + QLatin1Char('/') + name));
    }
    // What was copied is one step, the folder, so an undo can take it all away.
    const QList<JobStep> steps = job->steps();
    QCOMPARE(steps.size(), 1);
    QCOMPARE(steps.first().kind, JobStep::Created);
    QCOMPARE(steps.first().to, s.dst + QStringLiteral("/F"));

    // One undo removes everything the stopped copy made, and leaves the source alone.
    QVERIFY(history.canUndo());
    FileJob *undo = runUndo(history);
    QVERIFY(undo);
    QCOMPARE(undo->failureCount(), 0);
    QCOMPARE(namesIn(s.dst), QStringList());
    QCOMPARE(namesIn(s.trashFiles()), QStringList{QStringLiteral("F")});
    QCOMPARE(namesIn(s.trashFiles() + QStringLiteral("/F")), copied);
    QCOMPARE(namesIn(f).size(), 5000);
}

// One unreadable file of three: the other two are copied and the one failure names it.
void FileOpsTests::copyReportsWhatFailed()
{
    Scratch s;
    QStringList sources;
    for (const char *name : {"a.txt", "b.txt", "c.txt"}) {
        sources.append(s.src + QLatin1Char('/') + QLatin1String(name));
        QVERIFY(writeFile(sources.last(), name));
    }
    const QByteArray b = QFile::encodeName(sources.at(1));
    QVERIFY(::chmod(b.constData(), 0) == 0);

    JobQueue queue;
    auto *job = new CopyJob(sources, s.dst);
    QVERIFY(runJob(queue, job));
    ::chmod(b.constData(), 0644);
    QCOMPARE(job->state(), FileJob::State::Done);
    QCOMPARE(namesIn(s.dst), (QStringList{QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
    QCOMPARE(readFile(s.dst + QStringLiteral("/a.txt")), QByteArray("a.txt"));
    QCOMPARE(readFile(s.dst + QStringLiteral("/c.txt")), QByteArray("c.txt"));
    const QList<JobFailure> failures = job->failures();
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.first().path, sources.at(1));
    QVERIFY2(failures.first().message.contains(QStringLiteral("b.txt")), qPrintable(failures.first().message));
}

// 5,000 files: progress never goes back and ends at 5,000 files and their total bytes.
void FileOpsTests::progressIsReported()
{
    Scratch s;
    const QString f = s.src + QStringLiteral("/F");
    QVERIFY(QDir().mkpath(f));
    qint64 totalBytes = 0;
    for (int i = 0; i < 5000; ++i) {
        const int size = (i * 37) % 3000 + 1;
        totalBytes += size;
        QVERIFY(writeFile(f + QStringLiteral("/f%1").arg(i), QByteArray(size, char('a' + i % 26))));
    }

    JobQueue queue;
    auto *job = new CopyJob({f}, s.dst);
    struct Report { qint64 filesDone, filesTotal, bytesDone, bytesTotal; };
    QList<Report> reports;
    connect(job, &FileJob::progressChanged, this, [&](qint64 fd, qint64 ft, qint64 bd, qint64 bt) {
        reports.append({fd, ft, bd, bt});
    });
    QVERIFY(runJob(queue, job));
    QCOMPARE(job->failureCount(), 0);

    QVERIFY(!reports.isEmpty());
    for (qsizetype i = 0; i < reports.size(); ++i) {
        QCOMPARE(reports.at(i).filesTotal, qint64(5000));
        QCOMPARE(reports.at(i).bytesTotal, totalBytes);
        if (i > 0) {
            QVERIFY(reports.at(i).filesDone >= reports.at(i - 1).filesDone);
            QVERIFY(reports.at(i).bytesDone >= reports.at(i - 1).bytesDone);
        }
    }
    QCOMPARE(reports.last().filesDone, qint64(5000));
    QCOMPARE(reports.last().bytesDone, totalBytes);
    QCOMPARE(namesIn(s.dst + QStringLiteral("/F")).size(), 5000);
}

// Photos/ {a.jpg "old", b.jpg} in the destination; Photos/ {a.jpg "new", c.jpg} copied beside it:
// the folder clash offers Merge, Merge asks again about a.jpg, Keep both gives
// [a (2).jpg "new", a.jpg "old", b.jpg, c.jpg]; one undo gives [a.jpg "old", b.jpg] again.
void FileOpsTests::mergeFoldersAsksPerFile()
{
    Scratch s;
    const QString photos = s.dst + QStringLiteral("/Photos");
    const QString source = s.src + QStringLiteral("/Photos");
    QVERIFY(QDir().mkpath(photos));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(writeFile(photos + QStringLiteral("/a.jpg"), "old"));
    QVERIFY(writeFile(photos + QStringLiteral("/b.jpg"), "b"));
    QVERIFY(writeFile(source + QStringLiteral("/a.jpg"), "new"));
    QVERIFY(writeFile(source + QStringLiteral("/c.jpg"), "c"));

    JobQueue queue;
    UndoHistory history(&queue);
    auto *job = new CopyJob({source}, s.dst);
    QStringList asked;
    QList<bool> canMerge;
    connect(job, &FileJob::clashAsked, this, [&](const QString &from, const QString &target) {
        asked.append(from + QStringLiteral(" -> ") + target);
        canMerge.append(job->clashCanMerge());
        if (asked.size() == 1) {
            job->answerClash(FileJob::ClashChoice::Merge);
        } else {
            job->answerClash(FileJob::ClashChoice::Merge); // not a folder clash: ignored, still asking
            job->answerClash(FileJob::ClashChoice::KeepBoth);
        }
    });
    QVERIFY(runJob(queue, job));
    QCOMPARE(asked, (QStringList{source + QStringLiteral(" -> ") + photos,
                                 source + QStringLiteral("/a.jpg -> ") + photos + QStringLiteral("/a.jpg")}));
    QCOMPARE(canMerge, (QList<bool>{true, false}));
    QCOMPARE(job->failureCount(), 0);
    QCOMPARE(namesIn(photos), (QStringList{QStringLiteral("a (2).jpg"), QStringLiteral("a.jpg"),
                                           QStringLiteral("b.jpg"), QStringLiteral("c.jpg")}));
    QCOMPARE(readFile(photos + QStringLiteral("/a (2).jpg")), QByteArray("new"));
    QCOMPARE(readFile(photos + QStringLiteral("/a.jpg")), QByteArray("old"));
    QCOMPARE(namesIn(s.dst), QStringList{QStringLiteral("Photos")});
    QCOMPARE(job->targets(), QStringList{photos});

    QVERIFY(runUndo(history));
    QCOMPARE(namesIn(photos), (QStringList{QStringLiteral("a.jpg"), QStringLiteral("b.jpg")}));
    QCOMPARE(readFile(photos + QStringLiteral("/a.jpg")), QByteArray("old"));
    QCOMPARE(namesIn(source), (QStringList{QStringLiteral("a.jpg"), QStringLiteral("c.jpg")}));
}

QTEST_GUILESS_MAIN(FileOpsTests)
#include "fileops_tests.moc"
