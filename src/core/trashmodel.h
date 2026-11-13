// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"
#include "trash.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>

namespace Riley {

// What is in the trash, for the trash view: every thing in the files/ folder of each trash
// folder given (the home trash and each drive's .Trash-<uid>, see Trash::trashFolders), with
// where it came from and when it was deleted, read from its info file. A thing whose info file
// is missing or broken is still listed, with no original path, so it can be deleted for good.
// Sorted by name, case ignored, then by deletion date.
class RILEYCORE_EXPORT TrashModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, // the name it had: "a.txt"
        PathRole,                    // where it lives now, in a trash's files/
        OriginalPathRole,            // the path it had, or empty when its info file is unreadable
        OriginalFolderRole,          // the folder it was in
        DeletedRole,                 // its deletion date (local time)
        DeletedTextRole,             // the same, written for a person: "2026-11-13 14:05"
        IsDirRole,
    };
    Q_ENUM(Role)

    struct Item
    {
        QString name;
        TrashEntry entry; // trashedPath always set; originalPath empty when unknown
        bool isDir = false;
    };

    explicit TrashModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    QList<Item> items() const { return m_items; }
    // Every thing's path in its trash, in row order.
    QStringList paths() const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &trashedPath) const;

    // Reads these trash folders (each holding files/ and info/) again.
    void reload(const QStringList &trashFolders);

    // The things in these trash folders, as reload() lists them.
    static QList<Item> read(const QStringList &trashFolders);

signals:
    void countChanged();

private:
    QList<Item> m_items;
};

} // namespace Riley
