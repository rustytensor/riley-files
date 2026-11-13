// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

class QProcess;
class QTimer;

namespace Riley {

// The network places of the sidebar: the servers connected through GVfs, each a folder under
// $XDG_RUNTIME_DIR/gvfs/ named like "sftp:host=example.org,user=marc". A row shows the host's
// name and opens that folder. The folder is read again every two seconds, and the rows change
// only when the servers do.
//
// Connect to Server runs "gio mount <uri>", an argument list and never a shell, for sftp, smb,
// ftp, dav and davs addresses only.
class RILEYCORE_EXPORT NetworkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, // the host: "example.org"
        PathRole,                    // the GVfs folder it opens
        IconRole,                    // "network"
    };
    Q_ENUM(Role)

    struct Place
    {
        QString name;
        QString path;
    };

    explicit NetworkModel(QObject *parent = nullptr);
    ~NetworkModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_places.size()); }
    Q_INVOKABLE int rowOf(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    // The row whose folder holds path, or -1.
    int rowHolding(const QString &path) const;

    // Reads the GVfs folder again now.
    Q_INVOKABLE void reload();

    // Starts connecting to uri. connectFinished() says how it went. False when the address is
    // not one that can be connected to, gio is missing, or a connection is being made already;
    // error then says why.
    bool connectTo(const QString &uri, QString *error = nullptr);
    bool connecting() const { return m_process != nullptr; }

    // $XDG_RUNTIME_DIR/gvfs, or /run/user/<uid>/gvfs.
    static QString gvfsPath();
    // The host in a GVfs folder name ("sftp:host=h,user=u" -> "h", "smb-share:server=s,share=x"
    // -> "s"); the name itself when it names none.
    static QString hostOf(const QString &folderName);
    // True for an sftp, smb, ftp, dav or davs address with a host.
    static bool isConnectable(const QString &uri);

signals:
    void countChanged();
    void connectFinished(const QString &uri, bool ok, const QString &message);

private:
    void setPlaces(const QList<Place> &places);

    QList<Place> m_places;
    QTimer *m_poll = nullptr;
    QProcess *m_process = nullptr;
    QString m_uri;
};

} // namespace Riley
