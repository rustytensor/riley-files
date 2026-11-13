// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "iconprovider.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QUrl>

#include <algorithm>

namespace {

QMutex themeLock;
QString themeInUse;

constexpr int defaultWanted = 64;

QString homeDir()
{
    const QString home = qEnvironmentVariable("HOME");
    return home.isEmpty() ? QDir::homePath() : home;
}

QStringList dataDirs()
{
    QStringList dirs;
    QString home = qEnvironmentVariable("XDG_DATA_HOME");
    dirs.append(QDir::isAbsolutePath(home) ? home : homeDir() + QStringLiteral("/.local/share"));
    QStringList system;
    for (const QString &dir : qEnvironmentVariable("XDG_DATA_DIRS").split(u':', Qt::SkipEmptyParts)) {
        if (QDir::isAbsolutePath(dir))
            system.append(dir);
    }
    if (system.isEmpty())
        system = {QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")};
    dirs.append(system);
    return dirs;
}

// Smaller is better: the theme first, then the kind of image.
int rankOf(const QString &theme, const QString &relative, const QString &wantedTheme)
{
    int themeRank = 3;
    if (!wantedTheme.isEmpty() && theme == wantedTheme)
        themeRank = 0;
    else if (theme == QLatin1String("hicolor"))
        themeRank = 1;
    else if (theme.isEmpty())
        themeRank = 4; // a pixmaps folder
    // "48x48/places/x.png" or "places/48/x.svg": the largest number in the path is its size.
    int size = 0;
    const bool scalable = relative.contains(QLatin1String("scalable")) || relative.endsWith(QLatin1String(".svg"))
                          || relative.endsWith(QLatin1String(".svgz"));
    qsizetype i = 0;
    while (i < relative.size()) {
        if (relative.at(i).isDigit()) {
            int n = 0;
            while (i < relative.size() && relative.at(i).isDigit())
                n = n * 10 + relative.at(i++).digitValue();
            size = std::max(size, std::min(n, 1024));
        } else {
            ++i;
        }
    }
    return themeRank * 10000 + (scalable ? 0 : 2000 - size);
}

} // namespace

void IconIndex::setThemeName(const QString &name)
{
    QMutexLocker locker(&themeLock);
    themeInUse = name;
}

std::shared_ptr<const IconIndex> IconIndex::get()
{
    // Built again only when the folders it walks are named differently (another environment).
    static QMutex lock;
    static std::shared_ptr<const IconIndex> index;
    static QString builtFor;
    QString key = homeDir() + u'\n' + dataDirs().join(u':');
    {
        QMutexLocker theme(&themeLock);
        key += u'\n' + themeInUse;
    }
    QMutexLocker locker(&lock);
    if (!index || key != builtFor) {
        index = std::shared_ptr<const IconIndex>(new IconIndex());
        builtFor = key;
    }
    return index;
}

IconIndex::IconIndex()
{
    QString wantedTheme;
    {
        QMutexLocker locker(&themeLock);
        wantedTheme = themeInUse;
    }
    const QStringList kinds = {QStringLiteral("*.png"), QStringLiteral("*.svg"), QStringLiteral("*.svgz"),
                               QStringLiteral("*.xpm")};
    QStringList iconDirs{homeDir() + QStringLiteral("/.icons")};
    QStringList pixmapDirs;
    for (const QString &dir : dataDirs()) {
        iconDirs.append(dir + QStringLiteral("/icons"));
        pixmapDirs.append(dir + QStringLiteral("/pixmaps"));
    }

    QHash<QString, int> ranks;
    const auto consider = [&](const QString &file, int rank) {
        const QString fileName = QFileInfo(file).fileName();
        const qsizetype dot = fileName.lastIndexOf(u'.');
        if (dot <= 0)
            return;
        const QString name = fileName.left(dot);
        const auto at = ranks.constFind(name);
        if (at != ranks.cend() && *at <= rank)
            return;
        ranks.insert(name, rank);
        m_files.insert(name, file);
    };

    for (const QString &base : std::as_const(iconDirs)) {
        const QDir dir(base);
        const QStringList themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &theme : themes) {
            const QString themeDir = dir.filePath(theme);
            QDirIterator it(themeDir, kinds, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                consider(file, rankOf(theme, file.mid(themeDir.size()), wantedTheme));
            }
        }
    }
    for (const QString &base : std::as_const(pixmapDirs)) {
        QDirIterator it(base, kinds, QDir::Files);
        while (it.hasNext())
            consider(it.next(), rankOf(QString(), QString(), wantedTheme));
    }
    m_names = m_files.keys();
    std::sort(m_names.begin(), m_names.end(), [](const QString &a, const QString &b) {
        const int c = a.compare(b, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a < b;
    });
}

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

QString IconProvider::sourceFor(const QString &icon)
{
    if (icon.isEmpty())
        return {};
    return QStringLiteral("image://icon/") + QString::fromLatin1(QUrl::toPercentEncoding(icon));
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QString icon = id;
    if (icon.contains(u'%'))
        icon = QUrl::fromPercentEncoding(icon.toUtf8());
    const QString file = QDir::isAbsolutePath(icon) ? icon : IconIndex::get()->fileFor(icon);
    QImage image;
    if (!file.isEmpty()) {
        QImageReader reader(file);
        reader.setAutoTransform(true);
        const int wanted = std::max(requestedSize.width(), requestedSize.height()) > 0
                               ? std::max(requestedSize.width(), requestedSize.height())
                               : defaultWanted;
        QSize native = reader.size();
        if (!native.isValid() || native.isEmpty())
            native = QSize(wanted, wanted);
        reader.setScaledSize(native.scaled(wanted, wanted, Qt::KeepAspectRatio));
        image = reader.read();
    }
    if (size)
        *size = image.size();
    return image;
}
