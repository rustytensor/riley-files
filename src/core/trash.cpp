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

// The name of a thing in Trash/files, when trashedPath is directly in it.
std::optional<QString> trashedName(const QString &trashedPath)
{
    const QString clean = QDir::cleanPath(QFileInfo(trashedPath).absoluteFilePath());
    const QString filesDir = Trash::homeTrashPath() + QStringLiteral("/files");
    const QFileInfo info(clean);
    if (info.absolutePath() != filesDir || info.fileName().isEmpty())
        return std::nullopt;
    return info.fileName();
}

QString infoPathOf(const QString &name)
{
    return Trash::homeTrashPath() + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo");
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
    const QString trash = homeTrashPath();
    if (isSameOrInside(trash, original)) {
        if (error)
            *error = QStringLiteral("“%1” holds the trash itself and cannot go into it").arg(name);
        return std::nullopt;
    }
    const QString filesDir = trash + QStringLiteral("/files");
    const QString infoDir = trash + QStringLiteral("/info");
    // The data folder itself may not be there yet ($XDG_DATA_HOME on a fresh account).
    const QString dataDir = QFileInfo(trash).absolutePath();
    if (!QFileInfo(dataDir).isDir() && !QDir().mkpath(dataDir)) {
        if (error)
            *error = QStringLiteral("Cannot make the folder %1 for the trash").arg(dataDir);
        return std::nullopt;
    }
    if (!makeFolder(trash, error) || !makeFolder(filesDir, error) || !makeFolder(infoDir, error))
        return std::nullopt;

    // The info file is made first, exclusively: that claims the name, so two programs trashing
    // at once never pick the same one.
    const QDateTime now = QDateTime::currentDateTime();
    const QByteArray info = QByteArrayLiteral("[Trash Info]\nPath=")
                            + QUrl::toPercentEncoding(QString::fromLocal8Bit(QFile::encodeName(original)), "/")
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
    const std::optional<QString> name = trashedName(trashedPath);
    if (!name)
        return std::nullopt;
    QFile file(infoPathOf(*name));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    TrashEntry entry;
    entry.trashedPath = homeTrashPath() + QStringLiteral("/files/") + *name;
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
            // Percent-encoded bytes; an absolute path, or one relative to the drive's top
            // (only for other drives' trash folders, so not here).
            const QByteArray raw = QByteArray::fromPercentEncoding(line.mid(5));
            entry.originalPath = QDir::cleanPath(QFile::decodeName(raw));
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
    const std::optional<QString> name = trashedName(trashedPath);
    if (!name) {
        if (error)
            *error = QStringLiteral("“%1” is not in the trash").arg(QFileInfo(trashedPath).fileName());
        return std::nullopt;
    }
    const QString inTrash = homeTrashPath() + QStringLiteral("/files/") + *name;
    if (!pathExists(inTrash)) {
        if (error)
            *error = QStringLiteral("“%1” is no longer in the trash").arg(*name);
        return std::nullopt;
    }
    QString target = destination.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    if (target.isEmpty()) {
        const std::optional<TrashEntry> entry = entryFor(inTrash);
        if (!entry) {
            if (error)
                *error = QStringLiteral("The trash does not say where “%1” came from").arg(*name);
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
                                 : QStringLiteral("Cannot put “%1” back: %2").arg(targetName, systemError(e));
        }
        return std::nullopt;
    }
    ::unlink(QFile::encodeName(infoPathOf(*name)).constData());
    return target;
}

bool Trash::erase(const QString &trashedPath, QString *error)
{
    const std::optional<QString> name = trashedName(trashedPath);
    if (!name) {
        if (error)
            *error = QStringLiteral("“%1” is not in the trash").arg(QFileInfo(trashedPath).fileName());
        return false;
    }
    const QString inTrash = homeTrashPath() + QStringLiteral("/files/") + *name;
    if (pathExists(inTrash) && !removeForGood(inTrash, error))
        return false;
    // The info file goes last: a thing whose removal failed half-way keeps its record.
    ::unlink(QFile::encodeName(infoPathOf(*name)).constData());
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
