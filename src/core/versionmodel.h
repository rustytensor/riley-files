// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"
#include "snapshots.h"

#include <QAbstractListModel>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace Riley {

// The earlier versions of one file or folder, newest first, as rows for a list (see
// Snapshots::versionsOf). Setting the path returns at once: the snapshots are read on a worker
// thread and the rows arrive a moment later (loaded() says when). A new path drops the read
// still going for the one before.
//
// available says whether there are snapshots above the path at all: where there are none,
// Previous Versions is not offered.
class RILEYCORE_EXPORT VersionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NumberRole = Qt::UserRole + 1, // the oldest snapshot of the version
        DateRole,                      // QDateTime, local
        DateTextRole,                  // "2026-11-02 10:00"
        DescriptionRole,
        CopyPathRole,                  // the copy inside the snapshot
        IsDirRole,
        SizeRole,                      // bytes, qint64
        ModifiedRole,                  // the copy's modified time
    };
    Q_ENUM(Role)

    explicit VersionModel(QObject *parent = nullptr);
    ~VersionModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString &path);

    bool isAvailable() const { return m_available; }
    bool isLoading() const { return m_loading; }
    int count() const { return int(m_versions.size()); }
    QList<FileVersion> versions() const { return m_versions; }

    // Reads the versions of the same path again (after a restore, say).
    Q_INVOKABLE void reload();

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QString copyPathAt(int row) const;
    Q_INVOKABLE QDateTime dateAt(int row) const;
    Q_INVOKABLE QString dateTextAt(int row) const;

    // How a version's date is shown: "2026-11-02 10:00", local time.
    static QString dateText(const QDateTime &date);

    // Every thread the snapshots were read on. A test can check that the main thread is never among them.
    QSet<Qt::HANDLE> readingThreads() const;

signals:
    void pathChanged();
    void availableChanged();
    void loadingChanged();
    void countChanged();
    // The versions of the path have been read.
    void loaded();

private:
    void start();
    void apply(quint64 generation, bool available, const QList<FileVersion> &versions);

    QString m_path;
    bool m_available = false;
    bool m_loading = false;
    QList<FileVersion> m_versions;

    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<quint64>> m_wanted;
    mutable QMutex m_threadsLock;
    QSet<Qt::HANDLE> m_threads;
    QThreadPool m_pool;
};

} // namespace Riley
