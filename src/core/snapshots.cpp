// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "snapshots.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Riley {

namespace {

const QString snapshotsName = QStringLiteral(".snapshots");

QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString parentOf(const QString &path)
{
    return path == QLatin1String("/") ? QString() : QFileInfo(path).absolutePath();
}

QString joinPath(const QString &folder, const QString &name)
{
    return folder.endsWith(QLatin1Char('/')) ? folder + name : folder + QLatin1Char('/') + name;
}

bool isNumber(const char *name)
{
    if (!*name)
        return false;
    for (const char *c = name; *c; ++c) {
        if (*c < '0' || *c > '9')
            return false;
    }
    return std::strlen(name) <= 9;
}

bool isFolder(const QString &path)
{
    struct stat st;
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isRegularFile(const QString &path)
{
    struct stat st;
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 && S_ISREG(st.st_mode);
}

// The numbered folders of <base>/.snapshots that look like snapshots: <n>/info.xml and <n>/snapshot/.
QList<int> numbersIn(const QString &base, bool firstOnly)
{
    QList<int> numbers;
    const QString folder = joinPath(base, snapshotsName);
    DIR *dir = ::opendir(QFile::encodeName(folder).constData());
    if (!dir)
        return numbers;
    while (const dirent *entry = ::readdir(dir)) {
        if (!isNumber(entry->d_name))
            continue;
        const QString one = folder + QLatin1Char('/') + QString::fromLatin1(entry->d_name);
        if (isRegularFile(one + QStringLiteral("/info.xml")) && isFolder(one + QStringLiteral("/snapshot"))) {
            numbers.append(std::atoi(entry->d_name));
            if (firstOnly)
                break;
        }
    }
    ::closedir(dir);
    return numbers;
}

QDateTime localTime(const struct timespec &ts)
{
    return QDateTime::fromMSecsSinceEpoch(qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000);
}

bool sameBytes(const QByteArray &a, const QByteArray &b, const std::function<bool()> &shouldStop)
{
    const int fa = ::open(a.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fa < 0)
        return false;
    const int fb = ::open(b.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fb < 0) {
        ::close(fa);
        return false;
    }
    constexpr qsizetype chunk = 1024 * 1024;
    QByteArray ba(chunk, Qt::Uninitialized), bb(chunk, Qt::Uninitialized);
    auto readFull = [](int fd, char *into, qsizetype want) -> qsizetype {
        qsizetype got = 0;
        while (got < want) {
            const ssize_t n = ::read(fd, into + got, size_t(want - got));
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                return -1;
            if (n == 0)
                break;
            got += n;
        }
        return got;
    };
    bool same = true;
    for (;;) {
        if (shouldStop && shouldStop()) {
            same = false;
            break;
        }
        const qsizetype na = readFull(fa, ba.data(), chunk);
        const qsizetype nb = readFull(fb, bb.data(), chunk);
        if (na < 0 || nb < 0 || na != nb || std::memcmp(ba.constData(), bb.constData(), size_t(na)) != 0) {
            same = false;
            break;
        }
        if (na == 0)
            break;
    }
    ::close(fa);
    ::close(fb);
    return same;
}

QByteArray linkTarget(const QByteArray &path)
{
    QByteArray target(4096, '\0');
    const ssize_t n = ::readlink(path.constData(), target.data(), size_t(target.size()));
    if (n < 0)
        return QByteArray();
    target.truncate(n);
    return target;
}

} // namespace

QString Snapshots::readOnlyMessage()
{
    return QStringLiteral("Snapshots can't be changed.");
}

bool Snapshots::isInSnapshot(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QStringList parts = cleanAbsolute(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.contains(snapshotsName);
}

QString Snapshots::baseFor(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QString absolute = cleanAbsolute(path);
    if (isInSnapshot(absolute))
        return QString();
    // A path that is gone (a deleted file) is looked up from its folder.
    struct stat st;
    const bool exists = ::lstat(QFile::encodeName(absolute).constData(), &st) == 0;
    QString folder = exists && S_ISDIR(st.st_mode) ? absolute : parentOf(absolute);
    struct stat start;
    if (folder.isEmpty() || ::stat(QFile::encodeName(folder).constData(), &start) != 0)
        return QString();
    while (!folder.isEmpty()) {
        struct stat here;
        if (::stat(QFile::encodeName(folder).constData(), &here) != 0 || here.st_dev != start.st_dev)
            break; // another file system (or subvolume): its snapshots do not hold this file
        if (isFolder(joinPath(folder, snapshotsName)) && !numbersIn(folder, true).isEmpty())
            return folder;
        folder = parentOf(folder);
    }
    return QString();
}

bool Snapshots::readInfo(const QString &infoPath, Snapshot *snapshot)
{
    QFile file(infoPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QXmlStreamReader xml(&file);
    QString date, description, number;
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("snapshot"))
        return false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("date"))
            date = xml.readElementText().trimmed();
        else if (xml.name() == QLatin1String("description"))
            description = xml.readElementText().trimmed();
        else if (xml.name() == QLatin1String("num"))
            number = xml.readElementText().trimmed();
        else
            xml.skipCurrentElement();
    }
    const QStringList parts = date.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return false;
    const QDate day = QDate::fromString(parts.at(0), QStringLiteral("yyyy-MM-dd"));
    const QTime time = QTime::fromString(parts.at(1), QStringLiteral("HH:mm:ss"));
    if (!day.isValid() || !time.isValid())
        return false;
    snapshot->date = QDateTime(day, time, QTimeZone::UTC).toLocalTime();
    snapshot->description = description;
    bool ok = false;
    const int n = number.toInt(&ok);
    if (ok)
        snapshot->number = n;
    return true;
}

QList<Snapshot> Snapshots::snapshotsOf(const QString &base)
{
    QList<Snapshot> result;
    const QString folder = joinPath(cleanAbsolute(base), snapshotsName);
    for (const int number : numbersIn(cleanAbsolute(base), false)) {
        const QString one = folder + QLatin1Char('/') + QString::number(number);
        Snapshot snapshot;
        if (!readInfo(one + QStringLiteral("/info.xml"), &snapshot))
            continue;
        snapshot.number = number; // the folder is what the copies are found by
        snapshot.folder = one + QStringLiteral("/snapshot");
        result.append(snapshot);
    }
    std::sort(result.begin(), result.end(), [](const Snapshot &a, const Snapshot &b) {
        return a.date != b.date ? a.date < b.date : a.number < b.number;
    });
    return result;
}

QList<FileVersion> Snapshots::versionsOf(const QString &path, const std::function<bool()> &shouldStop)
{
    const QString absolute = cleanAbsolute(path);
    const QString base = baseFor(absolute);
    if (base.isEmpty())
        return {};
    const QString relative = absolute == base ? QString()
                             : absolute.mid(base == QLatin1String("/") ? 1 : base.size() + 1);

    struct stat live;
    const bool liveExists = ::lstat(QFile::encodeName(absolute).constData(), &live) == 0;
    const bool liveIsFolder = liveExists && S_ISDIR(live.st_mode);

    QList<FileVersion> versions;
    bool runOpen = false; // the last version's run can still grow
    QString runLast;      // the newest copy in that run
    for (const Snapshot &snapshot : snapshotsOf(base)) {
        if (shouldStop && shouldStop())
            return {};
        const QString copy = relative.isEmpty() ? snapshot.folder : joinPath(snapshot.folder, relative);
        struct stat st;
        if (::lstat(QFile::encodeName(copy).constData(), &st) != 0
            || (liveExists && S_ISDIR(st.st_mode) != liveIsFolder)) {
            runOpen = false;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            FileVersion version;
            version.snapshot = snapshot;
            version.numbers = {snapshot.number};
            version.path = copy;
            version.isFolder = true;
            version.modified = localTime(st.st_mtim);
            versions.append(version);
            runOpen = false;
            continue;
        }
        if (runOpen && sameContent(runLast, copy, shouldStop)) {
            versions.last().numbers.append(snapshot.number);
            runLast = copy;
            continue;
        }
        FileVersion version;
        version.snapshot = snapshot;
        version.numbers = {snapshot.number};
        version.path = copy;
        version.size = S_ISREG(st.st_mode) ? qint64(st.st_size) : 0;
        version.modified = localTime(st.st_mtim);
        versions.append(version);
        runOpen = true;
        runLast = copy;
    }
    if (shouldStop && shouldStop())
        return {};

    if (liveExists && !liveIsFolder) {
        versions.erase(std::remove_if(versions.begin(), versions.end(),
                                      [&](const FileVersion &v) { return sameContent(absolute, v.path, shouldStop); }),
                       versions.end());
    }
    if (shouldStop && shouldStop())
        return {};
    std::reverse(versions.begin(), versions.end());
    return versions;
}

QString Snapshots::copyName(const QString &name, const QDateTime &date, bool isFolder)
{
    const QString stamp = QStringLiteral(" (%1)").arg(date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH.mm")));
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (isFolder || dot <= 0 || dot == name.size() - 1)
        return name + stamp;
    return name.left(dot) + stamp + name.mid(dot);
}

bool Snapshots::sameContent(const QString &a, const QString &b, const std::function<bool()> &shouldStop)
{
    const QByteArray pa = QFile::encodeName(a), pb = QFile::encodeName(b);
    struct stat sa, sb;
    if (::lstat(pa.constData(), &sa) != 0 || ::lstat(pb.constData(), &sb) != 0)
        return false;
    if ((sa.st_mode & S_IFMT) != (sb.st_mode & S_IFMT))
        return false;
    if (S_ISLNK(sa.st_mode)) {
        const QByteArray ta = linkTarget(pa);
        return !ta.isNull() && ta == linkTarget(pb);
    }
    if (!S_ISREG(sa.st_mode) || sa.st_size != sb.st_size)
        return false;
    if (sa.st_mtim.tv_sec == sb.st_mtim.tv_sec && sa.st_mtim.tv_nsec == sb.st_mtim.tv_nsec)
        return true;
    return sameBytes(pa, pb, shouldStop);
}

} // namespace Riley
