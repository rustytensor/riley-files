// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QList>
#include <QString>

namespace Riley {

// The pinned folders of the sidebar, kept in the GTK 3 bookmarks file
// ($XDG_CONFIG_HOME/gtk-3.0/bookmarks, or ~/.config/gtk-3.0/bookmarks) so GTK file dialogs show
// the same list.
//
// Each line of that file is a URI, then optionally a space and a label. Local file:// lines are
// shown (by their label, else the folder's name); other lines (sftp://, smb://, ...) are kept in
// the file exactly as they are and not shown yet. Pinning adds a line at the end; unpinning
// removes only the lines of that folder; every other byte of the file stays as it was. The
// file is written whole under a temporary name and then renamed, and is read again when
// another program changes it.
class RILEYCORE_EXPORT PinnedModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, // the label, else the folder's name
        PathRole,                    // the folder, absolute
        IconRole,                    // "folder"
    };
    Q_ENUM(Role)

    struct Pin
    {
        QString name;
        QString path;
    };

    explicit PinnedModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_pins.size()); }
    QList<Pin> pins() const { return m_pins; }

    // The bookmarks file, from the environment as it was at the last reload().
    QString filePath() const { return m_file; }

    Q_INVOKABLE bool isPinned(const QString &path) const;
    // Adds path at the end (nothing when it is pinned already). False when the file could not
    // be written; error() then says why.
    Q_INVOKABLE bool pin(const QString &path);
    // Removes every line naming path, and only those.
    Q_INVOKABLE bool unpin(const QString &path);
    // Pins path, or unpins it when pinned (Ctrl+D). Returns whether it is pinned now.
    Q_INVOKABLE bool toggle(const QString &path);

    Q_INVOKABLE int rowOf(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;

    QString error() const { return m_error; }

    // Finds the file again from the environment and reads it.
    Q_INVOKABLE void reload();

    // The local folder a bookmarks line names, or nothing for a non-local or broken line; label
    // gets the line's label (empty when it has none).
    static QString pathOfLine(const QByteArray &line, QString *label = nullptr);

signals:
    void countChanged();

private:
    void read();
    bool write(const QList<QByteArray> &lines);
    void watch();

    QString m_file;
    QList<QByteArray> m_lines; // the file as read, split at '\n' (a final empty piece when it ends in one)
    QList<Pin> m_pins;
    QString m_error;
    QFileSystemWatcher m_watcher;
};

} // namespace Riley
