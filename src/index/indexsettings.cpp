// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "indexsettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <fnmatch.h>

namespace Riley {

namespace {

const QString Group = QStringLiteral("index");

QString cleaned(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    QString expanded = path.trimmed();
    if (expanded == u'~' || expanded.startsWith(QLatin1String("~/")))
        expanded = QDir::homePath() + expanded.mid(1);
    return QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
}

QStringList cleanedList(const QStringList &paths)
{
    QStringList list;
    for (const QString &path : paths) {
        const QString clean = cleaned(path);
        if (!clean.isEmpty() && !list.contains(clean))
            list.append(clean);
    }
    std::sort(list.begin(), list.end());
    return list;
}

QStringList nonEmpty(const QStringList &list)
{
    QStringList out;
    for (const QString &item : list)
        if (!item.trimmed().isEmpty())
            out.append(item.trimmed());
    return out;
}

bool matches(const QString &pattern, const QString &text)
{
    return ::fnmatch(QFile::encodeName(pattern).constData(), QFile::encodeName(text).constData(), 0) == 0;
}

} // namespace

bool isAtOrBelow(const QString &path, const QString &folder)
{
    if (path == folder)
        return true;
    if (folder == u'/')
        return path.startsWith(u'/');
    return path.size() > folder.size() && path.startsWith(folder) && path.at(folder.size()) == u'/';
}

IndexSettings IndexSettings::defaults()
{
    IndexSettings settings;
    settings.folders = {QDir::homePath()};
    settings.excludes = defaultExcludes();
    return settings;
}

QStringList IndexSettings::defaultExcludes()
{
    return {QStringLiteral(".cache"), QStringLiteral("node_modules"), QStringLiteral(".git"),
            QStringLiteral("build"),  QStringLiteral("target"),       QStringLiteral("__pycache__")};
}

QString IndexSettings::defaultFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/riley-files/riley-files.conf");
}

IndexSettings IndexSettings::load(QSettings &settings)
{
    IndexSettings out = defaults();
    settings.beginGroup(Group);
    out.enabled = settings.value(QStringLiteral("enabled"), out.enabled).toBool();
    if (settings.contains(QStringLiteral("folders")))
        out.folders = nonEmpty(settings.value(QStringLiteral("folders")).toStringList());
    if (settings.contains(QStringLiteral("excludes")))
        out.excludes = nonEmpty(settings.value(QStringLiteral("excludes")).toStringList());
    if (settings.contains(QStringLiteral("contentFolders")))
        out.contentFolders = nonEmpty(settings.value(QStringLiteral("contentFolders")).toStringList());
    bool ok = false;
    const qint64 limit = settings.value(QStringLiteral("contentLimit")).toLongLong(&ok);
    if (ok && limit >= 0)
        out.contentLimit = limit;
    out.textOnly = settings.value(QStringLiteral("textOnly"), out.textOnly).toBool();
    settings.endGroup();
    return out;
}

void IndexSettings::save(QSettings &settings) const
{
    settings.beginGroup(Group);
    settings.setValue(QStringLiteral("enabled"), enabled);
    settings.setValue(QStringLiteral("folders"), folders);
    settings.setValue(QStringLiteral("excludes"), excludes);
    settings.setValue(QStringLiteral("contentFolders"), contentFolders);
    settings.setValue(QStringLiteral("contentLimit"), contentLimit);
    settings.setValue(QStringLiteral("textOnly"), textOnly);
    settings.endGroup();
}

IndexSettings IndexSettings::loadFile(const QString &file)
{
    QSettings settings(file, QSettings::IniFormat);
    return load(settings);
}

bool IndexSettings::saveFile(const QString &file) const
{
    QSettings settings(file, QSettings::IniFormat);
    save(settings);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QStringList IndexSettings::roots() const
{
    return cleanedList(folders);
}

bool IndexSettings::isExcluded(const QString &name, const QString &path) const
{
    for (const QString &pattern : excludes) {
        if (pattern.startsWith(u'/')) {
            if (isAtOrBelow(path, QDir::cleanPath(pattern)))
                return true;
        } else if (pattern.contains(u'/')) {
            if (matches(pattern, path))
                return true;
        } else if (matches(pattern, name)) {
            return true;
        }
    }
    return false;
}

bool IndexSettings::contentsWanted(const QString &path) const
{
    const QStringList list = cleanedList(contentFolders);
    return std::any_of(list.begin(), list.end(), [&](const QString &folder) { return isAtOrBelow(path, folder); });
}

} // namespace Riley
