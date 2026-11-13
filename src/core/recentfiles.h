// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace Riley {

// The files used lately, shared with every other program through the freedesktop desktop
// bookmark file, $XDG_DATA_HOME/recently-used.xbel (or ~/.local/share/recently-used.xbel), as
// GTK reads and writes it.
//
// The model lists its local files that still exist, newest first (by each bookmark's modified
// date). record() adds a file opened here, or updates its bookmark: modified and visited now,
// and an application entry named "Riley Files" (exec 'riley-files %u') whose count goes up by
// one. Every other byte of the file, every other bookmark among them, stays as it was; the file
// is replaced in one step (written beside it, then renamed), so a reader never sees half of it.
class RILEYCORE_EXPORT RecentFiles : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, // the file's name
        PathRole,                    // its absolute path
        FolderRole,                  // the folder it is in
        ModifiedRole,                // when its bookmark was last changed (local time)
        ModifiedTextRole,            // the same, for a person: "2026-11-13 14:05"
        IsDirRole,
    };
    Q_ENUM(Role)

    // One <bookmark> element of the file.
    struct Bookmark
    {
        QString href;       // as written, a URI
        QString path;       // its local path, or empty when it is not a file:// URI
        QDateTime modified; // modified, else visited, else added
        qsizetype start = 0; // where the element starts in the text, and where it ends (after it)
        qsizetype end = 0;
    };

    struct Item
    {
        QString path;
        QDateTime modified;
        bool isDir = false;
    };

    explicit RecentFiles(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    QList<Item> items() const { return m_items; }
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &path) const;

    // Reads the file again.
    Q_INVOKABLE void reload();

    // The bookmark file.
    static QString filePath();
    // Every bookmark in an XBEL text, in its order. Nothing, and ok false, when the text is
    // not well-formed XML.
    static QList<Bookmark> parse(const QString &text, bool *ok = nullptr);
    // The existing local files among the bookmarks of the bookmark file, newest first.
    static QList<Item> read();

    // Adds path (a file opened here, of this MIME type) to the bookmark file, or updates its
    // bookmark, as said above. On failure the file is left as it was and error says why.
    static bool record(const QString &path, const QString &mimeType, QString *error = nullptr);
    // The same on a text: the new text, or nothing when the text cannot be read as XBEL.
    static std::optional<QString> recordIn(const QString &text, const QString &path, const QString &mimeType,
                                           const QDateTime &now);

signals:
    void countChanged();

private:
    QList<Item> m_items;
};

} // namespace Riley
