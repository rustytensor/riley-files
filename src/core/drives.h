// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QList>
#include <QString>

class QProcess;
class QTimer;

namespace Riley {

// One line of /proc/self/mountinfo: what is mounted where.
struct Mount
{
    QString device;     // the mount source, e.g. /dev/sdb1
    QString mountPoint; // where it is mounted, escapes undone
    QString fsType;     // vfat, ext4, ...
};

// The drives of the sidebar: the file systems the person mounted (a USB stick, a second disk),
// read from /proc/self/mountinfo (or the file named by $RILEY_MOUNTINFO, for tests).
//
// Only mounts directly under /run/media/<user>/, /media/ and /mnt/ are drives; the system's own
// (/, /home, /boot, /proc, /run/user/...) never are. The kernel does not reliably say when
// mountinfo changes, so it is read again every two seconds and the rows change only when the
// drives do.
//
// Eject unmounts a drive and then powers it off, through udisksctl (udisks2), started with an
// argument list and never through a shell: "unmount -b <device>", then, only when that
// succeeded, "power-off -b <device>". What udisksctl says on failure is passed on.
class RILEYCORE_EXPORT DrivesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1, // the mount point's last name: "USB"
        PathRole,                    // the mount point, the folder it opens
        IconRole,                    // "drive"
        DeviceRole,                  // the device, e.g. /dev/sdb1
    };
    Q_ENUM(Role)

    explicit DrivesModel(QObject *parent = nullptr);
    ~DrivesModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_drives.size()); }
    QList<Mount> drives() const { return m_drives; }

    Q_INVOKABLE int rowOf(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    // The row whose mount point holds path (the drive a file is on), or -1.
    int rowHolding(const QString &path) const;

    // Reads the mount table again now.
    Q_INVOKABLE void reload();

    // Starts ejecting the drive on row. ejectFinished() says how it went. False when there is
    // no such drive, it cannot be ejected (not a /dev device) or one is being ejected already;
    // error then says why.
    bool eject(int row, QString *error = nullptr);
    bool ejecting() const { return m_process != nullptr; }

    // The file the mounts are read from: $RILEY_MOUNTINFO, or /proc/self/mountinfo.
    static QString mountInfoPath();
    // Every mount in a mountinfo text, in its order.
    static QList<Mount> parseMountInfo(const QByteArray &text);
    // True for a mount point a person mounted: /run/media/<user>/<name>, /media/<name> or
    // /mnt/<name>.
    static bool isUserMount(const QString &mountPoint);
    // The drives among mounts, by name (case ignored), one per mount point.
    static QList<Mount> userDrives(const QList<Mount> &mounts);

signals:
    void countChanged();
    // ok when the drive was unmounted and powered off; otherwise message says what failed,
    // with udisksctl's own words.
    void ejectFinished(const QString &name, bool ok, const QString &message);

private:
    void setDrives(const QList<Mount> &drives);
    void runStep(const QString &step);
    void stepFinished(int exitCode, bool crashed);

    QList<Mount> m_drives;
    QTimer *m_poll = nullptr;
    QProcess *m_process = nullptr;
    QString m_step;          // "unmount" or "power-off"
    QString m_ejectDevice;
    QString m_ejectName;
};

} // namespace Riley
