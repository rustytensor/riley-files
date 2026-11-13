// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "places.h"

#include "trash.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Riley {

namespace {

QString homeFolder()
{
    const QString home = qEnvironmentVariable("HOME");
    return QDir::cleanPath(home.isEmpty() ? QDir::homePath() : home);
}

QString configFolder(const QString &home)
{
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    return QDir::isAbsolutePath(config) ? QDir::cleanPath(config) : home + QStringLiteral("/.config");
}

// The user folders in sidebar order: their user-dirs.dirs key, usual name and icon.
struct UserDir
{
    const char *key;
    const char *usualName;
    const char *icon;
};
constexpr UserDir userDirs[] = {
    {"DESKTOP", "Desktop", "desktop"},       {"DOCUMENTS", "Documents", "documents"},
    {"DOWNLOAD", "Downloads", "downloads"},  {"MUSIC", "Music", "music"},
    {"PICTURES", "Pictures", "pictures"},    {"VIDEOS", "Videos", "videos"},
};

} // namespace

PlacesModel::PlacesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

QHash<QString, QString> PlacesModel::parseUserDirs(const QByteArray &text, const QString &home)
{
    QHash<QString, QString> dirs;
    for (QByteArray line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const qsizetype eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QByteArray key = line.left(eq).trimmed();
        QByteArray value = line.mid(eq + 1).trimmed();
        if (!key.startsWith("XDG_") || !key.endsWith("_DIR") || value.size() < 2 || !value.startsWith('"')
            || !value.endsWith('"'))
            continue;
        value = value.mid(1, value.size() - 2);
        // The format allows \" and \\ inside the quotes.
        QByteArray unescaped;
        for (qsizetype i = 0; i < value.size(); ++i) {
            if (value.at(i) == '\\' && i + 1 < value.size())
                ++i;
            unescaped.append(value.at(i));
        }
        QString path = QFile::decodeName(unescaped);
        if (path == QLatin1String("$HOME"))
            path = home;
        else if (path.startsWith(QLatin1String("$HOME/")))
            path = home + path.mid(5);
        else if (!path.startsWith(QLatin1Char('/')))
            continue;
        dirs.insert(QString::fromLatin1(key.mid(4, key.size() - 8)), QDir::cleanPath(path));
    }
    return dirs;
}

void PlacesModel::reload()
{
    const QString home = homeFolder();
    QFile file(configFolder(home) + QStringLiteral("/user-dirs.dirs"));
    const QHash<QString, QString> named =
        file.open(QIODevice::ReadOnly) ? parseUserDirs(file.readAll(), home) : QHash<QString, QString>();

    QList<Place> places;
    places.append({QCoreApplication::translate("Places", "Home"), home, QStringLiteral("home")});
    for (const UserDir &dir : userDirs) {
        const QString key = QString::fromLatin1(dir.key);
        const QString path = named.contains(key) ? named.value(key)
                                                 : home + QLatin1Char('/') + QLatin1String(dir.usualName);
        if (path == home || !QFileInfo(path).isDir())
            continue;
        // A renamed folder ("Dokumente") is shown by its own name.
        const QString name = named.contains(key) ? QFileInfo(path).fileName() : QString::fromLatin1(dir.usualName);
        places.append({name, path, QString::fromLatin1(dir.icon)});
    }
    places.append({QCoreApplication::translate("Places", "Trash"), Trash::homeTrashPath() + QStringLiteral("/files"), QStringLiteral("trash")});

    const bool countChanges = places.size() != m_places.size();
    beginResetModel();
    m_places = places;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

int PlacesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_places.size());
}

QVariant PlacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_places.size())
        return QVariant();
    const Place &place = m_places.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return place.name;
    case PathRole: return place.path;
    case IconRole: return place.icon;
    }
    return QVariant();
}

QHash<int, QByteArray> PlacesModel::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {IconRole, "icon"}};
}

int PlacesModel::rowOf(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (qsizetype i = 0; i < m_places.size(); ++i) {
        if (m_places.at(i).path == clean)
            return int(i);
    }
    return -1;
}

QString PlacesModel::pathAt(int row) const
{
    return row >= 0 && row < m_places.size() ? m_places.at(row).path : QString();
}

QString PlacesModel::nameAt(int row) const
{
    return row >= 0 && row < m_places.size() ? m_places.at(row).name : QString();
}

} // namespace Riley
