// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "trashmodel.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace Riley {

TrashModel::TrashModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TrashModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant TrashModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return item.name;
    case PathRole:
        return item.entry.trashedPath;
    case OriginalPathRole:
        return item.entry.originalPath;
    case OriginalFolderRole:
        return item.entry.originalPath.isEmpty() ? QString() : QFileInfo(item.entry.originalPath).absolutePath();
    case DeletedRole:
        return item.entry.deletedAt;
    case DeletedTextRole:
        return item.entry.deletedAt.isValid() ? item.entry.deletedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                                              : QString();
    case IsDirRole:
        return item.isDir;
    default:
        return {};
    }
}

QHash<int, QByteArray> TrashModel::roleNames() const
{
    return {{NameRole, "name"},
            {PathRole, "path"},
            {OriginalPathRole, "originalPath"},
            {OriginalFolderRole, "originalFolder"},
            {DeletedRole, "deleted"},
            {DeletedTextRole, "deletedText"},
            {IsDirRole, "isDir"}};
}

QStringList TrashModel::paths() const
{
    QStringList list;
    list.reserve(m_items.size());
    for (const Item &item : m_items)
        list.append(item.entry.trashedPath);
    return list;
}

QString TrashModel::pathAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).entry.trashedPath : QString();
}

QString TrashModel::nameAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).name : QString();
}

int TrashModel::rowOf(const QString &trashedPath) const
{
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).entry.trashedPath == trashedPath)
            return int(i);
    }
    return -1;
}

QList<TrashModel::Item> TrashModel::read(const QStringList &trashFolders)
{
    QList<Item> items;
    for (const QString &folder : trashFolders) {
        const QDir files(folder + QStringLiteral("/files"));
        const QFileInfoList entries =
            files.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : entries) {
            Item item;
            const QString path = QDir::cleanPath(info.absoluteFilePath());
            if (const std::optional<TrashEntry> entry = Trash::entryFor(path))
                item.entry = *entry;
            item.entry.trashedPath = path;
            item.name = item.entry.originalPath.isEmpty() ? info.fileName() : QFileInfo(item.entry.originalPath).fileName();
            item.isDir = info.isDir() && !info.isSymLink();
            items.append(item);
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return a.entry.deletedAt < b.entry.deletedAt;
    });
    return items;
}

void TrashModel::reload(const QStringList &trashFolders)
{
    const QList<Item> items = read(trashFolders);
    const bool countChanges = items.size() != m_items.size();
    beginResetModel();
    m_items = items;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

} // namespace Riley
