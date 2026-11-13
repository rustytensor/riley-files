// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace Riley {

// The fixed places of the sidebar: Home, the XDG user folders that exist (Desktop, Documents,
// Downloads, Music, Pictures, Videos, in that order), then the Trash.
//
// The user folders come from $XDG_CONFIG_HOME/user-dirs.dirs (or ~/.config/user-dirs.dirs), as
// xdg-user-dirs writes it; a folder the file does not name is looked for under its usual name
// in the home folder. A folder that is missing, or set to the home folder itself (the way
// xdg-user-dirs turns one off), is left out. The environment is read again by reload().
class RILEYCORE_EXPORT PlacesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, // what the sidebar shows: "Home", "Documents", "Trash"
        PathRole,                    // the folder it opens
        IconRole,                    // home, desktop, documents, downloads, music, pictures, videos, trash
    };
    Q_ENUM(Role)

    struct Place
    {
        QString name;
        QString path;
        QString icon;
    };

    explicit PlacesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_places.size()); }
    QList<Place> places() const { return m_places; }

    // The row whose folder is exactly path, or -1.
    Q_INVOKABLE int rowOf(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;

    // Reads the home folder, user-dirs.dirs and the trash location again.
    Q_INVOKABLE void reload();

    // The XDG user folders named in a user-dirs.dirs text, by key ("DOCUMENTS" -> its absolute
    // path), with $HOME expanded. Lines it cannot read are ignored.
    static QHash<QString, QString> parseUserDirs(const QByteArray &text, const QString &home);

signals:
    void countChanged();

private:
    QList<Place> m_places;
};

} // namespace Riley
