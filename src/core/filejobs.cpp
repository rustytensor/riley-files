// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "filejobs.h"

#include "filenames.h"
#include "trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Riley {

namespace {

constexpr size_t chunkBytes = 4 * 1024 * 1024;

QString errorText(int code)
{
    return QString::fromLocal8Bit(std::strerror(code));
}

QString nameOf(const QByteArray &path)
{
    return QFileInfo(QFile::decodeName(path)).fileName();
}

QString pathOf(const QByteArray &path)
{
    return QFile::decodeName(path);
}

QByteArray joined(const QByteArray &folder, const QByteArray &name)
{
    return folder.endsWith('/') ? folder + name : folder + '/' + name;
}

QByteArray parentOf(const QByteArray &path)
{
    const qsizetype slash = path.lastIndexOf('/');
    return slash <= 0 ? QByteArray("/") : path.left(slash);
}

// The names in a folder, without "." and "..". Sets errno and returns false when unreadable.
bool namesIn(const QByteArray &folder, QList<QByteArray> *names)
{
    DIR *dir = ::opendir(folder.constData());
    if (!dir)
        return false;
    errno = 0;
    while (const dirent *entry = ::readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
            names->append(QByteArray(entry->d_name));
        errno = 0;
    }
    const int readErrno = errno;
    ::closedir(dir);
    errno = readErrno;
    return readErrno == 0;
}

struct Tally
{
    qint64 files = 0; // anything that is not a folder
    qint64 bytes = 0; // data in regular files
};

void tally(const QByteArray &path, const struct stat &st, Tally *total)
{
    if (!S_ISDIR(st.st_mode)) {
        total->files += 1;
        if (S_ISREG(st.st_mode))
            total->bytes += st.st_size;
        return;
    }
    QList<QByteArray> names;
    namesIn(path, &names);
    for (const QByteArray &name : std::as_const(names)) {
        const QByteArray child = joined(path, name);
        struct stat cst;
        if (::lstat(child.constData(), &cst) == 0)
            tally(child, cst, total);
    }
}

// Removes a tree this job made itself (a partial copy). Our folders may already carry the
// source's read-only mode, so they are made writable on the way down.
bool removeOwnCopy(const QByteArray &path)
{
    struct stat st;
    if (::lstat(path.constData(), &st) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(st.st_mode))
        return ::unlink(path.constData()) == 0;
    ::chmod(path.constData(), 0700);
    QList<QByteArray> names;
    bool ok = namesIn(path, &names);
    for (const QByteArray &name : std::as_const(names))
        ok = removeOwnCopy(joined(path, name)) && ok;
    return ::rmdir(path.constData()) == 0 && ok;
}

// Removes the original after a complete copy (a move across drives). Leaves the person's
// permissions alone: what cannot be removed stays, and errno says why.
bool removeOriginal(const QByteArray &path)
{
    struct stat st;
    if (::lstat(path.constData(), &st) != 0)
        return false;
    if (!S_ISDIR(st.st_mode))
        return ::unlink(path.constData()) == 0;
    QList<QByteArray> names;
    if (!namesIn(path, &names))
        return false;
    for (const QByteArray &name : std::as_const(names)) {
        if (!removeOriginal(joined(path, name)))
            return false;
    }
    return ::rmdir(path.constData()) == 0;
}

// Whether /proc/self/fd can name an unnamed (O_TMPFILE) file, which is how such a file gets a
// name without special rights.
bool procFdWorks()
{
    static const bool works = ::access("/proc/self/fd", X_OK) == 0;
    return works;
}

std::atomic<unsigned> tempCounter{0};

// Gives a finished temporary file its name, never replacing anything. Returns errno, 0 when done.
int placeNamedTemp(const QByteArray &temp, const QByteArray &dst)
{
    if (::renameat2(AT_FDCWD, temp.constData(), AT_FDCWD, dst.constData(), RENAME_NOREPLACE) == 0)
        return 0;
    if (errno != EINVAL && errno != ENOSYS)
        return errno;
    // The file system has no RENAME_NOREPLACE: a hard link fails just the same on a taken name.
    if (::link(temp.constData(), dst.constData()) != 0)
        return errno;
    ::unlink(temp.constData());
    return 0;
}

} // namespace

TransferJob::TransferJob(const QStringList &sources, const QString &destination, bool move, QObject *parent)
    : FileJob(parent)
    , m_sources(sources)
    , m_destination(QDir::cleanPath(QDir(destination).absolutePath()))
    , m_move(move)
{
}

QStringList TransferJob::targets() const
{
    QMutexLocker lock(&m_targetsMutex);
    return m_targets;
}

void TransferJob::run()
{
    Tally total;
    for (const QString &source : m_sources) {
        const QByteArray path = QFile::encodeName(QDir::cleanPath(QDir(source).absolutePath()));
        struct stat st;
        if (::lstat(path.constData(), &st) == 0)
            tally(path, st, &total);
    }
    setTotals(total.files, total.bytes);

    struct stat dst;
    if (::stat(QFile::encodeName(m_destination).constData(), &dst) != 0 || !S_ISDIR(dst.st_mode)) {
        addFailure(m_destination, QStringLiteral("The folder “%1” is not there").arg(QFileInfo(m_destination).fileName()));
        return;
    }
    for (const QString &source : m_sources) {
        if (stopRequested())
            return;
        transferOne(source);
    }
}

void TransferJob::transferOne(const QString &source)
{
    const QString srcPath = QDir::cleanPath(QDir(source).absolutePath());
    const QByteArray src = QFile::encodeName(srcPath);
    const QString name = QFileInfo(srcPath).fileName();
    const QString verb = m_move ? QStringLiteral("move") : QStringLiteral("copy");

    struct stat st;
    if (::lstat(src.constData(), &st) != 0) {
        addFailure(srcPath, QStringLiteral("Cannot %1 “%2”: %3").arg(verb, name, errorText(errno)));
        return;
    }
    Tally mine;
    tally(src, st, &mine);
    const bool isFolder = S_ISDIR(st.st_mode);

    if (isFolder && isSameOrInside(m_destination, srcPath)) {
        addFailure(srcPath, QStringLiteral("Cannot %1 the folder “%2” into itself").arg(verb, name));
        addDone(mine.files, mine.bytes);
        return;
    }
    if (m_move && isSameOrInside(QFileInfo(srcPath).absolutePath(), m_destination)
        && isSameOrInside(m_destination, QFileInfo(srcPath).absolutePath())) {
        addDone(mine.files, mine.bytes); // already there: nothing to do
        return;
    }

    QString targetName = name;
    QString target = m_destination + QLatin1Char('/') + targetName;
    struct stat tst;
    if (::lstat(QFile::encodeName(target).constData(), &tst) == 0) {
        if (tst.st_dev == st.st_dev && tst.st_ino == st.st_ino) {
            // Copying a thing onto itself: the only safe answer is a second copy beside it.
            targetName = freeName(m_destination, name, isFolder);
        } else if (isSameOrInside(srcPath, target)) {
            addFailure(srcPath, QStringLiteral("Cannot %1 “%2” over the folder that holds it").arg(verb, name));
            addDone(mine.files, mine.bytes);
            return;
        } else {
            const std::optional<ClashChoice> choice = askClash(srcPath, target);
            if (!choice)
                return; // stopped while asking
            switch (*choice) {
            case ClashChoice::Skip:
                addDone(mine.files, mine.bytes);
                return;
            case ClashChoice::KeepBoth:
                targetName = freeName(m_destination, name, isFolder);
                break;
            case ClashChoice::Replace: {
                QString why;
                const std::optional<TrashEntry> trashed = Trash::put(target, &why);
                if (!trashed) {
                    addFailure(srcPath, QStringLiteral("“%1” was not replaced: %2").arg(name, why));
                    addDone(mine.files, mine.bytes);
                    return;
                }
                addStep(JobStep::Trashed, target, trashed->trashedPath);
                break;
            }
            }
        }
        target = m_destination + QLatin1Char('/') + targetName;
    }
    const QByteArray dst = QFile::encodeName(target);

    if (m_move) {
        int renameErrno = EXDEV;
        if (!m_pretendOtherDrive)
            renameErrno = ::renameat2(AT_FDCWD, src.constData(), AT_FDCWD, dst.constData(), RENAME_NOREPLACE) == 0
                              ? 0 : errno;
        if (renameErrno == EINVAL || renameErrno == ENOSYS) {
            if (pathExists(target))
                renameErrno = EEXIST;
            else
                renameErrno = ::rename(src.constData(), dst.constData()) == 0 ? 0 : errno;
        }
        if (renameErrno == 0) {
            addStep(JobStep::Moved, srcPath, target);
            addDone(mine.files, mine.bytes);
            QMutexLocker lock(&m_targetsMutex);
            m_targets.append(target);
            return;
        }
        if (renameErrno != EXDEV) {
            addFailure(srcPath, QStringLiteral("Cannot move “%1”: %2").arg(name, errorText(renameErrno)));
            addDone(mine.files, mine.bytes);
            return;
        }
        // Another drive: copy everything, and only then remove the original.
        const qint64 failuresBefore = failureCount();
        const bool copied = copyEntry(src, dst, st, true);
        if (!copied || stopRequested()) {
            removeOwnCopy(dst);
            dropStep(JobStep::Created, srcPath, target);
            if (!stopRequested() && failureCount() == failuresBefore)
                addFailure(srcPath, QStringLiteral("Cannot move “%1”").arg(name));
            return;
        }
        if (!removeOriginal(src)) {
            addFailure(srcPath, QStringLiteral("“%1” was copied, but the original could not be removed: %2")
                                    .arg(name, errorText(errno)));
        } else {
            dropStep(JobStep::Created, srcPath, target);
            addStep(JobStep::Moved, srcPath, target);
        }
        QMutexLocker lock(&m_targetsMutex);
        m_targets.append(target);
        return;
    }

    copyEntry(src, dst, st, true);
    if (pathExists(target)) {
        QMutexLocker lock(&m_targetsMutex);
        m_targets.append(target);
    }
}

bool TransferJob::copyEntry(const QByteArray &src, const QByteArray &dst, const struct stat &st, bool top)
{
    if (stopRequested())
        return false;
    if (S_ISREG(st.st_mode))
        return copyFile(src, dst, st, top);
    if (S_ISDIR(st.st_mode))
        return copyFolder(src, dst, st, top);
    if (S_ISLNK(st.st_mode)) {
        QByteArray target(st.st_size > 0 ? st.st_size + 1 : 4096, '\0');
        const ssize_t n = ::readlink(src.constData(), target.data(), size_t(target.size()));
        if (n < 0 || n >= target.size()) {
            addFailure(pathOf(src), QStringLiteral("Cannot read the link “%1”: %2").arg(nameOf(src), errorText(errno)));
            addDone(1, 0);
            return false;
        }
        target.truncate(n);
        if (::symlink(target.constData(), dst.constData()) != 0) {
            addFailure(pathOf(src), QStringLiteral("Cannot copy the link “%1”: %2").arg(nameOf(src), errorText(errno)));
            addDone(1, 0);
            return false;
        }
        const struct timespec times[2] = {st.st_atim, st.st_mtim};
        ::utimensat(AT_FDCWD, dst.constData(), times, AT_SYMLINK_NOFOLLOW);
        if (top)
            addStep(JobStep::Created, pathOf(src), pathOf(dst));
        addDone(1, 0);
        return true;
    }
    addFailure(pathOf(src), QStringLiteral("“%1” is a special file and was not copied").arg(nameOf(src)));
    addDone(1, 0);
    return false;
}

bool TransferJob::copyFile(const QByteArray &src, const QByteArray &dst, const struct stat &st, bool top)
{
    const qint64 size = st.st_size;
    qint64 copied = 0;
    auto failed = [&](const QString &message) {
        addFailure(pathOf(src), message);
        addDone(1, qMax<qint64>(0, size - copied));
        return false;
    };

    const int in = ::open(src.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0)
        return failed(QStringLiteral("Cannot read “%1”: %2").arg(nameOf(src), errorText(errno)));

    // The copy is written under no name where the file system allows it, else under a hidden
    // temporary name; it gets its real name only when complete.
    const QByteArray folder = parentOf(dst);
    QByteArray temp;
    int out = -1;
    if (procFdWorks())
        out = ::open(folder.constData(), O_TMPFILE | O_WRONLY | O_CLOEXEC, 0600);
    if (out < 0) {
        temp = joined(folder, "." + dst.mid(dst.lastIndexOf('/') + 1) + ".part-" + QByteArray::number(::getpid()) + '-'
                                  + QByteArray::number(tempCounter.fetch_add(1)));
        out = ::open(temp.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    }
    if (out < 0) {
        const int e = errno;
        ::close(in);
        return failed(QStringLiteral("Cannot write “%1”: %2").arg(nameOf(dst), errorText(e)));
    }
    auto discard = [&] {
        ::close(out);
        if (!temp.isEmpty())
            ::unlink(temp.constData());
        ::close(in);
    };

    bool useRange = true;
    QByteArray buffer;
    for (;;) {
        if (stopRequested()) {
            discard();
            return false;
        }
        ssize_t n = -1;
        if (useRange) {
            n = ::copy_file_range(in, nullptr, out, nullptr, chunkBytes, 0);
            if (n < 0 && copied == 0
                && (errno == EXDEV || errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP || errno == EBADF)) {
                useRange = false;
                continue;
            }
        } else {
            if (buffer.isEmpty())
                buffer.resize(1024 * 1024);
            n = ::read(in, buffer.data(), size_t(buffer.size()));
            if (n > 0) {
                ssize_t written = 0;
                while (written < n) {
                    const ssize_t w = ::write(out, buffer.constData() + written, size_t(n - written));
                    if (w < 0 && errno == EINTR)
                        continue;
                    if (w < 0) {
                        n = -1;
                        break;
                    }
                    written += w;
                }
            }
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            const int e = errno;
            discard();
            return failed(QStringLiteral("Cannot copy “%1”: %2").arg(nameOf(src), errorText(e)));
        }
        if (n == 0)
            break;
        copied += n;
        addDone(0, n);
    }

    ::fchmod(out, st.st_mode & 07777);
    const struct timespec times[2] = {st.st_atim, st.st_mtim};
    ::futimens(out, times);

    int placeErrno = 0;
    if (temp.isEmpty()) {
        const QByteArray fdPath = "/proc/self/fd/" + QByteArray::number(out);
        if (::linkat(AT_FDCWD, fdPath.constData(), AT_FDCWD, dst.constData(), AT_SYMLINK_FOLLOW) != 0)
            placeErrno = errno;
    }
    const int closeErrno = ::close(out) == 0 ? 0 : errno;
    if (placeErrno == 0)
        placeErrno = closeErrno;
    if (placeErrno == 0 && !temp.isEmpty())
        placeErrno = placeNamedTemp(temp, dst);
    ::close(in);
    if (placeErrno != 0) {
        if (!temp.isEmpty())
            ::unlink(temp.constData());
        addFailure(pathOf(src), placeErrno == EEXIST
                                    ? QStringLiteral("“%1” appeared in the destination meanwhile and was not overwritten")
                                          .arg(nameOf(dst))
                                    : QStringLiteral("Cannot write “%1”: %2").arg(nameOf(dst), errorText(placeErrno)));
        addDone(1, 0);
        return false;
    }
    if (top)
        addStep(JobStep::Created, pathOf(src), pathOf(dst));
    addDone(1, 0);
    return true;
}

bool TransferJob::copyFolder(const QByteArray &src, const QByteArray &dst, const struct stat &st, bool top)
{
    QList<QByteArray> names;
    if (!namesIn(src, &names)) {
        addFailure(pathOf(src), QStringLiteral("Cannot read the folder “%1”: %2").arg(nameOf(src), errorText(errno)));
        return false;
    }
    if (::mkdir(dst.constData(), 0700) != 0) {
        addFailure(pathOf(src), QStringLiteral("Cannot make the folder “%1”: %2").arg(nameOf(dst), errorText(errno)));
        return false;
    }
    if (top)
        addStep(JobStep::Created, pathOf(src), pathOf(dst));

    bool ok = true;
    for (const QByteArray &name : std::as_const(names)) {
        if (stopRequested()) {
            ok = false;
            break;
        }
        const QByteArray from = joined(src, name);
        struct stat cst;
        if (::lstat(from.constData(), &cst) != 0) {
            if (errno != ENOENT) { // a file removed meanwhile is simply not copied
                addFailure(pathOf(from), QStringLiteral("Cannot copy “%1”: %2").arg(nameOf(from), errorText(errno)));
                ok = false;
            }
            continue;
        }
        ok = copyEntry(from, joined(dst, name), cst, false) && ok;
    }

    ::chmod(dst.constData(), st.st_mode & 07777);
    const struct timespec times[2] = {st.st_atim, st.st_mtim};
    ::utimensat(AT_FDCWD, dst.constData(), times, 0);
    return ok;
}

CopyJob::CopyJob(const QStringList &sources, const QString &destination, QObject *parent)
    : TransferJob(sources, destination, false, parent)
{
}

MoveJob::MoveJob(const QStringList &sources, const QString &destination, QObject *parent)
    : TransferJob(sources, destination, true, parent)
{
}

RenameJob::RenameJob(const QString &path, const QString &newName, QObject *parent)
    : FileJob(parent)
    , m_path(QDir::cleanPath(QDir(path).absolutePath()))
    , m_newName(newName)
    , m_newPath(QFileInfo(m_path).absolutePath() + QLatin1Char('/') + newName)
{
}

void RenameJob::run()
{
    const QString oldName = QFileInfo(m_path).fileName();
    if (!isValidFileName(m_newName)) {
        addFailure(m_path, QStringLiteral("“%1” cannot be used as a name").arg(m_newName));
        return;
    }
    if (!pathExists(m_path)) {
        addFailure(m_path, QStringLiteral("“%1” is not there any more").arg(oldName));
        return;
    }
    setTotals(1, 0);
    if (m_newName == oldName) {
        addDone(1, 0);
        return;
    }
    const QByteArray from = QFile::encodeName(m_path);
    const QByteArray to = QFile::encodeName(m_newPath);
    int e = ::renameat2(AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) == 0 ? 0 : errno;
    if (e == EINVAL || e == ENOSYS) {
        // Without RENAME_NOREPLACE, check first. A name differing only in case may be the same
        // file on a case-insensitive drive; renaming it is what the person asked.
        struct stat a, b;
        const bool taken = ::lstat(to.constData(), &b) == 0
                           && !(::lstat(from.constData(), &a) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino);
        e = taken ? EEXIST : (::rename(from.constData(), to.constData()) == 0 ? 0 : errno);
    }
    if (e == EEXIST) {
        addFailure(m_path, QStringLiteral("There is already something named “%1” here").arg(m_newName));
        return;
    }
    if (e != 0) {
        addFailure(m_path, QStringLiteral("Cannot rename “%1”: %2").arg(oldName, errorText(e)));
        return;
    }
    addStep(JobStep::Moved, m_path, m_newPath);
    addDone(1, 0);
}

NewFolderJob::NewFolderJob(const QString &parent, const QString &name, QObject *owner)
    : FileJob(owner)
    , m_parent(QDir::cleanPath(QDir(parent).absolutePath()))
    , m_name(name)
{
}

QString NewFolderJob::createdPath() const
{
    QMutexLocker lock(&m_createdMutex);
    return m_created;
}

void NewFolderJob::run()
{
    if (!isValidFileName(m_name)) {
        addFailure(m_parent, QStringLiteral("“%1” cannot be used as a name").arg(m_name));
        return;
    }
    setTotals(1, 0);
    for (int tries = 0; tries < 1000 && !stopRequested(); ++tries) {
        const QString path = m_parent + QLatin1Char('/') + freeName(m_parent, m_name, true);
        if (::mkdir(QFile::encodeName(path).constData(), 0777) == 0) {
            {
                QMutexLocker lock(&m_createdMutex);
                m_created = path;
            }
            addStep(JobStep::Created, QString(), path);
            addDone(1, 0);
            return;
        }
        if (errno != EEXIST) {
            addFailure(m_parent, QStringLiteral("Cannot make a folder in “%1”: %2")
                                     .arg(QFileInfo(m_parent).fileName(), errorText(errno)));
            return;
        }
        // Taken between looking and making: look again.
    }
}

} // namespace Riley
