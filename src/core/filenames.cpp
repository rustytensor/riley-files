// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "filenames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

namespace Riley {

namespace {

// Splits a file name into the part that gets the number and the extension that follows it.
void splitName(const QString &name, bool isFolder, QString *base, QString *ext)
{
    *base = name;
    ext->clear();
    if (isFolder)
        return;
    int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1)
        return; // no extension, or a hidden name such as ".bashrc"
    // "x.tar.gz" numbers as "x (2).tar.gz".
    const int inner = name.lastIndexOf(QLatin1Char('.'), dot - 1);
    if (inner > 0 && name.mid(inner + 1, dot - inner - 1).compare(QLatin1String("tar"), Qt::CaseInsensitive) == 0)
        dot = inner;
    *base = name.left(dot);
    *ext = name.mid(dot);
}

// "a (4)" -> base "a", number 4. Without a number, number is 1.
void splitNumber(const QString &base, QString *plain, int *number)
{
    static const QRegularExpression numbered(QStringLiteral("^(.+) \\((\\d{1,9})\\)$"));
    const QRegularExpressionMatch m = numbered.match(base);
    if (m.hasMatch()) {
        *plain = m.captured(1);
        *number = m.captured(2).toInt();
    } else {
        *plain = base;
        *number = 1;
    }
}

// Absolute path with symlinks and ".." resolved as far as the path exists; the part that does
// not exist yet is appended as written.
QString resolved(const QString &path)
{
    QString existing = QDir::cleanPath(QDir(path).absolutePath());
    QStringList rest;
    while (!existing.isEmpty()) {
        const QFileInfo info(existing);
        if (info.exists()) {
            QString canon = info.canonicalFilePath();
            for (auto it = rest.crbegin(); it != rest.crend(); ++it)
                canon = QDir::cleanPath(canon + QLatin1Char('/') + *it);
            return canon;
        }
        const int slash = existing.lastIndexOf(QLatin1Char('/'));
        if (slash < 0)
            break;
        rest.append(existing.mid(slash + 1));
        existing = slash == 0 ? QStringLiteral("/") : existing.left(slash);
        if (existing == QLatin1String("/") && !QFileInfo(existing).exists())
            break;
    }
    return QDir::cleanPath(QDir(path).absolutePath());
}

} // namespace

QString numberedName(const QString &name, int number, bool isFolder)
{
    QString base, ext, plain;
    int had = 1;
    splitName(name, isFolder, &base, &ext);
    splitNumber(base, &plain, &had);
    return plain + QStringLiteral(" (") + QString::number(number) + QLatin1Char(')') + ext;
}

QString freeName(const QString &folder, const QString &name, bool isFolder)
{
    const QDir dir(folder);
    if (!pathExists(dir.filePath(name)))
        return name;
    QString base, ext, plain;
    int had = 1;
    splitName(name, isFolder, &base, &ext);
    splitNumber(base, &plain, &had);
    for (int n = qMax(2, had + 1);; ++n) {
        const QString candidate = numberedName(name, n, isFolder);
        if (!pathExists(dir.filePath(candidate)))
            return candidate;
    }
}

bool pathExists(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0;
}

bool isSameOrInside(const QString &inner, const QString &outer)
{
    const QString in = resolved(inner);
    const QString out = resolved(outer);
    if (in == out)
        return true;
    if (out == QLatin1String("/"))
        return in.startsWith(QLatin1Char('/'));
    return in.startsWith(out + QLatin1Char('/'));
}

bool isValidFileName(const QString &name)
{
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
           && !name.contains(QLatin1Char('/')) && !name.contains(QChar(0));
}

int renameNoReplace(const QString &from, const QString &to)
{
    const QByteArray f = QFile::encodeName(from);
    const QByteArray t = QFile::encodeName(to);
    if (::renameat2(AT_FDCWD, f.constData(), AT_FDCWD, t.constData(), RENAME_NOREPLACE) == 0)
        return 0;
    if (errno != EINVAL && errno != ENOSYS)
        return errno;
    if (pathExists(to))
        return EEXIST;
    return ::rename(f.constData(), t.constData()) == 0 ? 0 : errno;
}

} // namespace Riley
