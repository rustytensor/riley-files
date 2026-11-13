// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "thumbnailcache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageWriter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrl>

namespace Riley::ThumbnailCache {

namespace {

constexpr auto privateFolder = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
constexpr auto privateFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

QString hashedName(const QString &path)
{
    const QByteArray uri = uriOf(path).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex())
           + QStringLiteral(".png");
}

// Makes `folder` and every folder between it and the cache root private to the user.
bool makePrivateFolder(const QString &folder)
{
    if (!QDir().mkpath(folder))
        return false;
    const QString top = root();
    for (QString at = QDir::cleanPath(folder); at.startsWith(top); at = QFileInfo(at).absolutePath()) {
        QFile::setPermissions(at, privateFolder);
        if (at == top)
            break;
    }
    return true;
}

// Text compressed as PNG does it (zlib), at most 64 KiB of it.
QByteArray inflated(const QByteArray &zlib)
{
    constexpr quint32 most = 64 * 1024;
    QByteArray sized(4, '\0');
    sized[0] = char((most >> 24) & 0xff);
    sized[1] = char((most >> 16) & 0xff);
    sized[2] = char((most >> 8) & 0xff);
    sized[3] = char(most & 0xff);
    return qUncompress(sized + zlib);
}

// The text chunks (tEXt, zTXt, iTXt) in front of a PNG's pixels. Read by hand: QImageReader::text()
// cuts a key at its first colon, and "Thumb::URI" has two; QImage would decode every pixel.
QHash<QString, QString> pngText(const QString &png)
{
    QHash<QString, QString> keys;
    QFile file(png);
    if (!file.open(QIODevice::ReadOnly) || file.read(8) != QByteArray("\x89PNG\r\n\x1a\n", 8))
        return keys;
    constexpr quint32 largestChunk = 1 << 20;
    for (;;) {
        const QByteArray head = file.read(8);
        if (head.size() != 8)
            break;
        const quint32 length = (quint32(uchar(head[0])) << 24) | (quint32(uchar(head[1])) << 16)
                               | (quint32(uchar(head[2])) << 8) | quint32(uchar(head[3]));
        const QByteArray type = head.mid(4, 4);
        if (type == "IDAT" || type == "IEND" || length > largestChunk)
            break;
        const QByteArray data = file.read(length);
        if (data.size() != qsizetype(length) || file.read(4).size() != 4)
            break;
        const qsizetype nul = data.indexOf('\0');
        if (nul <= 0)
            continue;
        const QString key = QString::fromLatin1(data.left(nul));
        if (type == "tEXt") {
            keys.insert(key, QString::fromLatin1(data.mid(nul + 1)));
        } else if (type == "zTXt" && data.size() > nul + 2 && data[nul + 1] == 0) {
            keys.insert(key, QString::fromLatin1(inflated(data.mid(nul + 2))));
        } else if (type == "iTXt" && data.size() > nul + 3) {
            // Compression flag and method, then the language tag and translated key, each ended
            // by a zero, then the text in UTF-8.
            const bool compressed = data[nul + 1] != 0;
            const qsizetype language = data.indexOf('\0', nul + 3);
            const qsizetype translated = language < 0 ? -1 : data.indexOf('\0', language + 1);
            if (translated < 0)
                continue;
            const QByteArray text = data.mid(translated + 1);
            keys.insert(key, QString::fromUtf8(compressed ? inflated(text) : text));
        }
    }
    return keys;
}

} // namespace

QString root()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                           + QStringLiteral("/thumbnails"));
}

QString uriOf(const QString &path)
{
    return QString::fromLatin1(
        QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toEncoded(QUrl::FullyEncoded));
}

int bucketFor(int wanted)
{
    return wanted <= 128 ? 128 : 256;
}

QString bucketName(int bucket)
{
    return bucket <= 128 ? QStringLiteral("normal") : QStringLiteral("large");
}

QString thumbnailPath(const QString &path, int wanted)
{
    return root() + u'/' + bucketName(bucketFor(wanted)) + u'/' + hashedName(path);
}

QString failPath(const QString &path)
{
    return root() + QStringLiteral("/fail/riley-files/") + hashedName(path);
}

qint64 modifiedSeconds(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return -1;
    return info.lastModified(QTimeZone::UTC).toSecsSinceEpoch();
}

bool isCurrent(const QString &thumbnail, const QString &path)
{
    if (!QFileInfo::exists(thumbnail))
        return false;
    const QHash<QString, QString> keys = pngText(thumbnail);
    const QString uri = keys.value(QStringLiteral("Thumb::URI"));
    const QString mtime = keys.value(QStringLiteral("Thumb::MTime"));
    if (uri != uriOf(path))
        return false;
    bool ok = false;
    const qint64 seconds = mtime.toLongLong(&ok);
    return ok && seconds == modifiedSeconds(path);
}

bool store(const QImage &image, const QString &target, const QString &path)
{
    if (image.isNull() || !makePrivateFolder(QFileInfo(target).absolutePath()))
        return false;
    const QFileInfo source(path);

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    // The keys go on the image itself: QImageWriter::setText() would cut "Thumb::URI" at its
    // first colon.
    QImage keyed = image;
    keyed.setText(QStringLiteral("Thumb::URI"), uriOf(path));
    keyed.setText(QStringLiteral("Thumb::MTime"), QString::number(modifiedSeconds(path)));
    keyed.setText(QStringLiteral("Thumb::Size"), QString::number(source.size()));
    keyed.setText(QStringLiteral("Software"), QStringLiteral("Riley Files"));
    QImageWriter writer(&file, "png");
    if (!writer.write(keyed)) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return false;
    QFile::setPermissions(target, privateFile);
    return true;
}

} // namespace Riley::ThumbnailCache
