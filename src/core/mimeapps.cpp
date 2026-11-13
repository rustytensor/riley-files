// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "mimeapps.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace Riley {

namespace {

const QString defaultGroup = QStringLiteral("Default Applications");
const QString addedGroup = QStringLiteral("Added Associations");
const QString removedGroup = QStringLiteral("Removed Associations");

QString homeDir()
{
    const QString home = qEnvironmentVariable("HOME");
    return home.isEmpty() ? QDir::homePath() : home;
}

QString baseDir(const char *variable, const QString &fallback)
{
    const QString value = qEnvironmentVariable(variable);
    return QDir::isAbsolutePath(value) ? QDir::cleanPath(value) : fallback;
}

QStringList baseDirs(const char *variable, const QStringList &fallback)
{
    QStringList dirs;
    for (const QString &dir : qEnvironmentVariable(variable).split(u':', Qt::SkipEmptyParts)) {
        if (QDir::isAbsolutePath(dir))
            dirs.append(QDir::cleanPath(dir));
    }
    return dirs.isEmpty() ? fallback : dirs;
}

QString configHome()
{
    return baseDir("XDG_CONFIG_HOME", homeDir() + QStringLiteral("/.config"));
}

QString dataHome()
{
    return baseDir("XDG_DATA_HOME", homeDir() + QStringLiteral("/.local/share"));
}

QStringList dataDirs()
{
    return baseDirs("XDG_DATA_DIRS", {QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")});
}

QStringList desktopNames()
{
    QStringList names;
    for (const QString &name : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts))
        names.append(name.toLower());
    return names;
}

// One mimeapps.list: group -> MIME type -> desktop ids, in the file's order.
using Groups = QHash<QString, QHash<QString, QStringList>>;

Groups readList(const QString &path)
{
    Groups groups;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return groups;
    QString group;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0 || group.isEmpty())
            continue;
        QStringList ids;
        for (const QString &id : line.mid(eq + 1).split(u';', Qt::SkipEmptyParts)) {
            if (!id.trimmed().isEmpty())
                ids.append(id.trimmed());
        }
        groups[group][line.left(eq).trimmed()].append(ids);
    }
    return groups;
}

bool canRun(const QString &program)
{
    if (QDir::isAbsolutePath(program)) {
        const QFileInfo info(program);
        return info.isFile() && info.isExecutable();
    }
    return !QStandardPaths::findExecutable(program).isEmpty();
}

// Every application found, in the order of the folders (an id seen first hides later ones),
// with the ids that are hidden or unusable left out.
struct Installed
{
    QList<DesktopEntry> apps;
    QHash<QString, qsizetype> byId;
};

Installed scanApplications()
{
    Installed installed;
    QSet<QString> seen;
    for (const QString &dir : MimeApps::applicationDirs()) {
        if (!QFileInfo(dir).isDir())
            continue;
        QStringList files;
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files.append(it.next());
        files.sort();
        const QDir base(dir);
        for (const QString &path : files) {
            QString id = base.relativeFilePath(path);
            id.replace(u'/', u'-');
            if (seen.contains(id))
                continue;
            seen.insert(id);
            DesktopEntry entry = DesktopEntry::read(path, id);
            if (!entry.isValid())
                continue;
            if (!entry.tryExec.isEmpty() && !canRun(entry.tryExec))
                continue;
            installed.byId.insert(id, installed.apps.size());
            installed.apps.append(entry);
        }
    }
    return installed;
}

// The type itself (its canonical name and the name given), then what it is a kind of.
QStringList typesOf(const QString &mimeType)
{
    QStringList types{mimeType};
    const QMimeType type = QMimeDatabase().mimeTypeForName(mimeType);
    if (type.isValid()) {
        if (!types.contains(type.name()))
            types.append(type.name());
        for (const QString &parent : type.allAncestors()) {
            if (!types.contains(parent))
                types.append(parent);
        }
    }
    return types;
}

QString explicitDefault(const QStringList &types, const QList<Groups> &lists, const Installed &installed)
{
    for (const QString &type : types) {
        for (const Groups &list : lists) {
            for (const QString &id : list.value(defaultGroup).value(type)) {
                if (installed.byId.contains(id))
                    return id;
            }
        }
    }
    return {};
}

QList<Groups> readLists()
{
    QList<Groups> lists;
    for (const QString &path : MimeApps::listFiles())
        lists.append(readList(path));
    return lists;
}

} // namespace

QStringList MimeApps::listFiles()
{
    const QStringList desktops = desktopNames();
    QStringList dirs{configHome()};
    dirs.append(baseDirs("XDG_CONFIG_DIRS", {QStringLiteral("/etc/xdg")}));
    dirs.append(dataHome() + QStringLiteral("/applications"));
    for (const QString &dir : dataDirs())
        dirs.append(dir + QStringLiteral("/applications"));
    QStringList files;
    for (const QString &dir : dirs) {
        for (const QString &desktop : desktops)
            files.append(dir + u'/' + desktop + QStringLiteral("-mimeapps.list"));
        files.append(dir + QStringLiteral("/mimeapps.list"));
    }
    files.removeDuplicates();
    return files;
}

QStringList MimeApps::applicationDirs()
{
    QStringList dirs{dataHome() + QStringLiteral("/applications")};
    for (const QString &dir : dataDirs())
        dirs.append(dir + QStringLiteral("/applications"));
    dirs.removeDuplicates();
    return dirs;
}

QString MimeApps::userListFile()
{
    return configHome() + QStringLiteral("/mimeapps.list");
}

QList<DesktopEntry> MimeApps::allApplications()
{
    QList<DesktopEntry> apps = scanApplications().apps;
    std::stable_sort(apps.begin(), apps.end(), [](const DesktopEntry &a, const DesktopEntry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

DesktopEntry MimeApps::application(const QString &id)
{
    const Installed installed = scanApplications();
    const auto at = installed.byId.constFind(id);
    return at == installed.byId.cend() ? DesktopEntry() : installed.apps.at(*at);
}

QList<DesktopEntry> MimeApps::applicationsFor(const QString &mimeType)
{
    const Installed installed = scanApplications();
    const QList<Groups> lists = readLists();
    const QStringList types = typesOf(mimeType);

    QStringList ids;
    const auto add = [&](const QString &id) {
        if (installed.byId.contains(id) && !ids.contains(id))
            ids.append(id);
    };
    add(explicitDefault(types, lists, installed));
    for (const QString &type : types) {
        QSet<QString> removed;
        for (const Groups &list : lists) {
            // A file's removals hide what it and the files after it associate.
            for (const QString &id : list.value(removedGroup).value(type))
                removed.insert(id);
            for (const QString &id : list.value(defaultGroup).value(type)) {
                if (!removed.contains(id))
                    add(id);
            }
            for (const QString &id : list.value(addedGroup).value(type)) {
                if (!removed.contains(id))
                    add(id);
            }
        }
        for (const DesktopEntry &app : installed.apps) {
            if (app.mimeTypes.contains(type) && !removed.contains(app.id))
                add(app.id);
        }
    }

    QList<DesktopEntry> apps;
    for (const QString &id : ids)
        apps.append(installed.apps.at(installed.byId.value(id)));
    return apps;
}

QString MimeApps::defaultFor(const QString &mimeType)
{
    return explicitDefault(typesOf(mimeType), readLists(), scanApplications());
}

bool MimeApps::setDefault(const QString &mimeType, const QString &desktopId)
{
    const QString path = userListFile();
    QByteArray content;
    {
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly))
                return false;
            content = file.readAll();
        }
    }
    QList<QByteArray> lines = content.split('\n');
    if (content.isEmpty())
        lines.clear();
    const QByteArray entry = mimeType.toUtf8() + '=' + desktopId.toUtf8();
    const QByteArray header = '[' + defaultGroup.toUtf8() + ']';

    qsizetype start = -1;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == header) {
            start = i;
            break;
        }
    }
    if (start < 0) {
        // No such group yet: it goes at the end, after a blank line.
        if (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines.append(QByteArray());
        lines << header << entry << QByteArray();
    } else {
        qsizetype end = start + 1;
        while (end < lines.size() && !lines.at(end).trimmed().startsWith('['))
            ++end;
        qsizetype found = -1;
        qsizetype lastFilled = start;
        for (qsizetype i = start + 1; i < end; ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (line.isEmpty())
                continue;
            lastFilled = i;
            const qsizetype eq = line.indexOf('=');
            if (eq > 0 && !line.startsWith('#') && line.left(eq).trimmed() == mimeType.toUtf8())
                found = i;
        }
        if (found >= 0)
            lines[found] = entry;
        else
            lines.insert(lastFilled + 1, entry);
        if (lines.last().size() != 0)
            lines.append(QByteArray());
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(lines.join('\n'));
    return out.commit();
}

bool MimeApps::launch(const DesktopEntry &app, const QStringList &paths, QString *error)
{
    const QList<QStringList> lines = app.commandLines(paths);
    if (lines.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("MimeApps", "“%1” cannot be started: its command line is broken.").arg(app.name);
        return false;
    }
    for (const QStringList &line : lines) {
        QString program = line.first();
        if (!QDir::isAbsolutePath(program))
            program = QStandardPaths::findExecutable(program);
        if (program.isEmpty() || !canRun(program)) {
            if (error)
                *error = QCoreApplication::translate("MimeApps", "“%1” cannot be started: “%2” is not installed.").arg(app.name, line.first());
            return false;
        }
        if (!QProcess::startDetached(program, line.mid(1), homeDir())) {
            if (error)
                *error = QCoreApplication::translate("MimeApps", "“%1” could not be started.").arg(app.name);
            return false;
        }
    }
    return true;
}

} // namespace Riley
