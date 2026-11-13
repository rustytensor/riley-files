// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "pinnedfolders.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

namespace Riley {

namespace {

QString bookmarksFile()
{
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!QDir::isAbsolutePath(config)) {
        const QString home = qEnvironmentVariable("HOME");
        config = (home.isEmpty() ? QDir::homePath() : home) + QStringLiteral("/.config");
    }
    return QDir::cleanPath(config) + QStringLiteral("/gtk-3.0/bookmarks");
}

QString folderName(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

} // namespace

PinnedModel::PinnedModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { read(); watch(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { read(); watch(); });
    reload();
}

QString PinnedModel::pathOfLine(const QByteArray &line, QString *label)
{
    QByteArray text = line;
    if (text.endsWith('\r'))
        text.chop(1);
    const qsizetype space = text.indexOf(' ');
    const QByteArray uri = space < 0 ? text : text.left(space);
    if (label)
        *label = space < 0 ? QString() : QString::fromUtf8(text.mid(space + 1)).trimmed();
    if (!uri.startsWith("file://"))
        return QString();
    const QUrl url = QUrl::fromEncoded(uri);
    if (!url.isValid() || !url.isLocalFile())
        return QString();
    const QString path = url.toLocalFile();
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

void PinnedModel::reload()
{
    m_file = bookmarksFile();
    read();
    watch();
}

void PinnedModel::read()
{
    QFile file(m_file);
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly))
        bytes = file.readAll();
    m_lines = bytes.isEmpty() ? QList<QByteArray>() : bytes.split('\n');

    QList<Pin> pins;
    for (const QByteArray &line : std::as_const(m_lines)) {
        QString label;
        const QString path = pathOfLine(line, &label);
        if (!path.isEmpty())
            pins.append({label.isEmpty() ? folderName(path) : label, path});
    }
    const bool countChanges = pins.size() != m_pins.size();
    beginResetModel();
    m_pins = pins;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void PinnedModel::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    // The folder too: programs that save by renaming replace the file, which ends a watch on it.
    const QString folder = QFileInfo(m_file).absolutePath();
    if (QFileInfo(folder).isDir())
        m_watcher.addPath(folder);
    if (QFileInfo::exists(m_file))
        m_watcher.addPath(m_file);
}

bool PinnedModel::write(const QList<QByteArray> &lines)
{
    const QString folder = QFileInfo(m_file).absolutePath();
    if (!QDir().mkpath(folder)) {
        m_error = QCoreApplication::translate("PinnedFolders", "Cannot make the folder “%1”").arg(folder);
        return false;
    }
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = QCoreApplication::translate("PinnedFolders", "Cannot write the pinned folders: %1").arg(file.errorString());
        return false;
    }
    const QByteArray bytes = lines.join('\n');
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = QCoreApplication::translate("PinnedFolders", "Cannot write the pinned folders: %1").arg(file.errorString());
        return false;
    }
    m_error.clear();
    read();
    watch();
    return true;
}

bool PinnedModel::isPinned(const QString &path) const
{
    return rowOf(path) >= 0;
}

bool PinnedModel::pin(const QString &path)
{
    if (path.isEmpty())
        return false;
    read(); // another program may have changed it since
    if (isPinned(path))
        return true;
    QList<QByteArray> lines = m_lines;
    // Keep the file's lines as they are; the new one goes after the last, ending in a newline.
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    lines.append(QUrl::fromLocalFile(QDir::cleanPath(path)).toEncoded());
    lines.append(QByteArray());
    return write(lines);
}

bool PinnedModel::unpin(const QString &path)
{
    read();
    const QString clean = QDir::cleanPath(path);
    QList<QByteArray> lines;
    bool found = false;
    for (const QByteArray &line : std::as_const(m_lines)) {
        if (pathOfLine(line) == clean)
            found = true;
        else
            lines.append(line);
    }
    return !found || write(lines);
}

bool PinnedModel::toggle(const QString &path)
{
    if (isPinned(path)) {
        unpin(path);
        return isPinned(path);
    }
    pin(path);
    return isPinned(path);
}

int PinnedModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_pins.size());
}

QVariant PinnedModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_pins.size())
        return QVariant();
    const Pin &pin = m_pins.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return pin.name;
    case PathRole: return pin.path;
    case IconRole: return QStringLiteral("folder");
    }
    return QVariant();
}

QHash<int, QByteArray> PinnedModel::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {IconRole, "icon"}};
}

int PinnedModel::rowOf(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (qsizetype i = 0; i < m_pins.size(); ++i) {
        if (m_pins.at(i).path == clean)
            return int(i);
    }
    return -1;
}

QString PinnedModel::pathAt(int row) const
{
    return row >= 0 && row < m_pins.size() ? m_pins.at(row).path : QString();
}

QString PinnedModel::nameAt(int row) const
{
    return row >= 0 && row < m_pins.size() ? m_pins.at(row).name : QString();
}

} // namespace Riley
