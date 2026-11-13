// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "trash.h"

#include "filenames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Riley {

namespace {

QString systemError(int code)
{
    return QString::fromLocal8Bit(std::strerror(code));
}

bool makeFolder(const QString &path, QString *error)
{
    if (::mkdir(QFile::encodeName(path).constData(), 0700) == 0 || errno == EEXIST)
        return true;
    if (error)
        *error = QStringLiteral("Cannot make the trash folder %1: %2").arg(path, systemError(errno));
    return false;
}

bool writeAll(int fd, const QByteArray &bytes)
{
    qsizetype done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.constData() + done, size_t(bytes.size() - done));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        done += n;
    }
    return true;
}

// Where a thing in a trash is: its trash folder, the top folder its drive is mounted on (empty
// for the home trash, whose info files hold absolute paths) and its name in files/.
struct Place
{
    QString trash;
    QString top;
    QString name;

    QString filePath() const { return trash + QStringLiteral("/files/") + name; }
    QString infoPath() const { return trash + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo"); }
};

QString driveTrashName()
{
    return QStringLiteral(".Trash-%1").arg(::getuid());
}

// Where trashedPath is, when it is directly in the files/ folder of the home trash or of a
// drive's .Trash-<uid>.
std::optional<Place> locate(const QString &trashedPath)
{
    const QFileInfo info(QDir::cleanPath(QFileInfo(trashedPath).absoluteFilePath()));
    const QString name = info.fileName();
    const QFileInfo files(info.absolutePath());
    if (name.isEmpty() || files.fileName() != QLatin1String("files"))
        return std::nullopt;
    const QString trash = files.absolutePath();
    if (trash == Trash::homeTrashPath())
        return Place{trash, QString(), name};
    const QFileInfo trashInfo(trash);
    if (trashInfo.fileName() == driveTrashName())
        return Place{trash, trashInfo.absolutePath(), name};
    return std::nullopt;
}

// The device of the nearest folder that exists on the way up from path (the one a folder made
// at path would be on).
std::optional<dev_t> deviceOfNearest(QString path)
{
    struct stat st;
    for (;;) {
        if (::stat(QFile::encodeName(path).constData(), &st) == 0)
            return st.st_dev;
        if (path == QLatin1String("/") || path.isEmpty())
            return std::nullopt;
        path = QFileInfo(path).absolutePath();
    }
}

// A drive's trash folder that is there already must be a real folder (not a symlink someone
// else planted) owned by this user; one that is not there is made, private.
bool claimDriveTrash(const QString &trash, QString *error)
{
    const QByteArray p = QFile::encodeName(trash);
    struct stat st;
    if (::lstat(p.constData(), &st) == 0) {
        if (!S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) {
            if (error)
                *error = QStringLiteral("The trash folder %1 is not yours, so nothing is put in it").arg(trash);
            return false;
        }
        return true;
    }
    if (::mkdir(p.constData(), 0700) == 0 || errno == EEXIST)
        return true;
    if (error)
        *error = QStringLiteral("This drive has no trash and one cannot be made (%1)").arg(systemError(errno));
    return false;
}

int removeOne(const char *path, const struct stat *, int type, struct FTW *)
{
    return (type == FTW_DP ? ::rmdir(path) : ::unlink(path)) == 0 ? 0 : errno;
}

} // namespace

QString Trash::homeTrashPath()
{
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty() || !QDir::isAbsolutePath(data))
        data = QDir::homePath() + QStringLiteral("/.local/share");
    return QDir::cleanPath(data + QStringLiteral("/Trash"));
}

QString Trash::topDirOf(const QString &path)
{
    QString at = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    struct stat st;
    if (::lstat(QFile::encodeName(at).constData(), &st) != 0)
        return QString();
    const dev_t device = st.st_dev;
    while (at != QLatin1String("/")) {
        const QString parent = QFileInfo(at).absolutePath();
        if (::stat(QFile::encodeName(parent).constData(), &st) != 0 || st.st_dev != device)
            return at;
        at = parent;
    }
    return at;
}

QString Trash::driveTrashPath(const QString &topDir)
{
    const QString top = QDir::cleanPath(topDir);
    return top == QLatin1String("/") ? QLatin1Char('/') + driveTrashName() : top + QLatin1Char('/') + driveTrashName();
}

QString Trash::trashPathFor(const QString &path, QString *error)
{
    const QString original = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    struct stat st;
    if (::lstat(QFile::encodeName(original).constData(), &st) != 0) {
        if (error)
            *error = QStringLiteral("“%1” is not there any more").arg(QFileInfo(original).fileName());
        return QString();
    }
    const QString home = homeTrashPath();
    const std::optional<dev_t> homeDevice = deviceOfNearest(home);
    if (homeDevice && *homeDevice == st.st_dev)
        return home;
    const QString top = topDirOf(original);
    if (top.isEmpty() || top == original) {
        if (error)
            *error = QStringLiteral("“%1” is where a drive is mounted and cannot go into the trash")
                         .arg(QFileInfo(original).fileName());
        return QString();
    }
    return driveTrashPath(top);
}

bool Trash::isInTrash(const QString &path)
{
    return locate(path).has_value();
}

QStringList Trash::trashFolders(const QStringList &driveTops)
{
    QStringList folders{homeTrashPath()};
    for (const QString &top : driveTops) {
        const QString trash = driveTrashPath(top);
        struct stat st;
        if (!folders.contains(trash) && ::lstat(QFile::encodeName(trash).constData(), &st) == 0
            && S_ISDIR(st.st_mode) && st.st_uid == ::getuid())
            folders.append(trash);
    }
    return folders;
}

std::optional<TrashEntry> Trash::put(const QString &path, QString *error)
{
    const QFileInfo given(path);
    const QString original = QDir::cleanPath(given.absoluteFilePath());
    const QString name = given.fileName();
    if (!pathExists(original)) {
        if (error)
            *error = QStringLiteral("“%1” is not there any more").arg(name);
        return std::nullopt;
    }
    const QString trash = trashPathFor(original, error);
    if (trash.isEmpty())
        return std::nullopt;
    if (isSameOrInside(trash, original)) {
        if (error)
            *error = QStringLiteral("“%1” holds the trash itself and cannot go into it").arg(name);
        return std::nullopt;
    }
    const bool home = trash == homeTrashPath();
    const QString filesDir = trash + QStringLiteral("/files");
    const QString infoDir = trash + QStringLiteral("/info");
    if (home) {
        // The data folder itself may not be there yet ($XDG_DATA_HOME on a fresh account).
        const QString dataDir = QFileInfo(trash).absolutePath();
        if (!QFileInfo(dataDir).isDir() && !QDir().mkpath(dataDir)) {
            if (error)
                *error = QStringLiteral("Cannot make the folder %1 for the trash").arg(dataDir);
            return std::nullopt;
        }
        if (!makeFolder(trash, error))
            return std::nullopt;
    } else if (!claimDriveTrash(trash, error)) {
        return std::nullopt;
    }
    if (!makeFolder(filesDir, error) || !makeFolder(infoDir, error))
        return std::nullopt;

    // The home trash names the absolute path; a drive's trash the path below the drive's top
    // folder, so it still fits when the drive is mounted somewhere else.
    QString named = original;
    if (!home) {
        const QString top = QFileInfo(trash).absolutePath();
        named = top == QLatin1String("/") ? original.mid(1) : original.mid(top.size() + 1);
    }

    // The info file is made first, exclusively: that claims the name, so two programs trashing
    // at once never pick the same one.
    const QDateTime now = QDateTime::currentDateTime();
    const QByteArray info = QByteArrayLiteral("[Trash Info]\nPath=")
                            + QUrl::toPercentEncoding(QString::fromLocal8Bit(QFile::encodeName(named)), "/")
                            + QByteArrayLiteral("\nDeletionDate=")
                            + now.toString(QStringLiteral("yyyy-MM-ddThh:mm:ss")).toLatin1()
                            + QByteArrayLiteral("\n");
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = n == 1 ? name : numberedName(name, n, given.isDir() && !given.isSymLink());
        const QString infoPath = infoDir + QLatin1Char('/') + candidate + QStringLiteral(".trashinfo");
        const QString trashedPath = filesDir + QLatin1Char('/') + candidate;
        const int fd = ::open(QFile::encodeName(infoPath).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            if (error)
                *error = QStringLiteral("Cannot write to the trash: %1").arg(systemError(errno));
            return std::nullopt;
        }
        if (pathExists(trashedPath)) { // a leftover without its info file: leave it alone
            ::close(fd);
            ::unlink(QFile::encodeName(infoPath).constData());
            continue;
        }
        const bool written = writeAll(fd, info) && ::fsync(fd) == 0;
        const int writeErrno = errno;
        ::close(fd);
        if (!written) {
            ::unlink(QFile::encodeName(infoPath).constData());
            if (error)
                *error = QStringLiteral("Cannot write to the trash: %1").arg(systemError(writeErrno));
            return std::nullopt;
        }
        if (::rename(QFile::encodeName(original).constData(), QFile::encodeName(trashedPath).constData()) != 0) {
            const int moveErrno = errno;
            ::unlink(QFile::encodeName(infoPath).constData());
            if (error) {
                *error = moveErrno == EXDEV
                             ? QStringLiteral("“%1” is on another drive than the trash").arg(name)
                             : QStringLiteral("Cannot move “%1” to the trash: %2").arg(name, systemError(moveErrno));
            }
            return std::nullopt;
        }
        return TrashEntry{trashedPath, infoPath, original, now};
    }
    if (error)
        *error = QStringLiteral("The trash has no free name for “%1”").arg(name);
    return std::nullopt;
}

std::optional<TrashEntry> Trash::entryFor(const QString &trashedPath)
{
    const std::optional<Place> place = locate(trashedPath);
    if (!place)
        return std::nullopt;
    QFile file(place->infoPath());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    TrashEntry entry;
    entry.trashedPath = place->filePath();
    entry.infoPath = file.fileName();
    bool inGroup = false;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.startsWith('[')) {
            inGroup = line == "[Trash Info]";
            continue;
        }
        if (!inGroup)
            continue;
        if (line.startsWith("Path=")) {
            // Percent-encoded bytes: an absolute path, or in a drive's trash one relative to
            // the drive's top folder.
            QString named = QFile::decodeName(QByteArray::fromPercentEncoding(line.mid(5)));
            if (!place->top.isEmpty() && !named.isEmpty() && !QDir::isAbsolutePath(named))
                named = place->top + QLatin1Char('/') + named;
            entry.originalPath = QDir::cleanPath(named);
        } else if (line.startsWith("DeletionDate=")) {
            entry.deletedAt = QDateTime::fromString(QString::fromLatin1(line.mid(13)),
                                                    QStringLiteral("yyyy-MM-ddThh:mm:ss"));
        }
    }
    if (entry.originalPath.isEmpty() || !QDir::isAbsolutePath(entry.originalPath))
        return std::nullopt;
    return entry;
}

std::optional<QString> Trash::restore(const QString &trashedPath, const QString &destination, QString *error)
{
    const std::optional<Place> place = locate(trashedPath);
    if (!place) {
        if (error)
            *error = QStringLiteral("“%1” is not in the trash").arg(QFileInfo(trashedPath).fileName());
        return std::nullopt;
    }
    const QString inTrash = place->filePath();
    if (!pathExists(inTrash)) {
        if (error)
            *error = QStringLiteral("“%1” is no longer in the trash").arg(place->name);
        return std::nullopt;
    }
    QString target = destination.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    if (target.isEmpty()) {
        const std::optional<TrashEntry> entry = entryFor(inTrash);
        if (!entry) {
            if (error)
                *error = QStringLiteral("The trash does not say where “%1” came from").arg(place->name);
            return std::nullopt;
        }
        target = entry->originalPath;
    }
    const QString targetName = QFileInfo(target).fileName();
    const QString parent = QFileInfo(target).absolutePath();
    if (!QFileInfo(parent).isDir() && !QDir().mkpath(parent)) {
        if (error)
            *error = QStringLiteral("Cannot make the folder %1 again to put “%2” back").arg(parent, targetName);
        return std::nullopt;
    }
    const int e = renameNoReplace(inTrash, target);
    if (e != 0) {
        if (error) {
            *error = e == EEXIST ? QStringLiteral("There is already something named “%1” in %2").arg(targetName, parent)
                     : e == EXDEV ? QStringLiteral("“%1” cannot go back to %2: it is on another drive now").arg(targetName, parent)
                                  : QStringLiteral("Cannot put “%1” back: %2").arg(targetName, systemError(e));
        }
        return std::nullopt;
    }
    ::unlink(QFile::encodeName(place->infoPath()).constData());
    return target;
}

bool Trash::erase(const QString &trashedPath, QString *error)
{
    const std::optional<Place> place = locate(trashedPath);
    if (!place) {
        if (error)
            *error = QStringLiteral("“%1” is not in the trash").arg(QFileInfo(trashedPath).fileName());
        return false;
    }
    const QString inTrash = place->filePath();
    if (pathExists(inTrash) && !removeForGood(inTrash, error))
        return false;
    // The info file goes last: a thing whose removal failed half-way keeps its record.
    ::unlink(QFile::encodeName(place->infoPath()).constData());
    return true;
}

bool Trash::removeForGood(const QString &path, QString *error)
{
    const QByteArray p = QFile::encodeName(path);
    struct stat st;
    if (::lstat(p.constData(), &st) != 0) {
        if (error)
            *error = QStringLiteral("“%1” is not there any more").arg(QFileInfo(path).fileName());
        return false;
    }
    const int e = S_ISDIR(st.st_mode) ? ::nftw(p.constData(), removeOne, 32, FTW_DEPTH | FTW_PHYS)
                                      : (::unlink(p.constData()) == 0 ? 0 : errno);
    if (e != 0) {
        if (error) {
            *error = QStringLiteral("Cannot delete “%1”: %2")
                         .arg(QFileInfo(path).fileName(), e > 0 ? systemError(e) : systemError(errno));
        }
        return false;
    }
    return true;
}

} // namespace Riley
