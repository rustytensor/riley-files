// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "drives.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace Riley {

namespace {

constexpr int pollMsecs = 2000;

// mountinfo writes a space, tab, newline and backslash in a path as \040, \011, \012, \134.
QString unescape(const QByteArray &field)
{
    QByteArray out;
    out.reserve(field.size());
    for (qsizetype i = 0; i < field.size(); ++i) {
        const char c = field.at(i);
        if (c == '\\' && i + 3 < field.size()) {
            const QByteArray digits = field.mid(i + 1, 3);
            bool ok = std::all_of(digits.begin(), digits.end(), [](char d) {
                return d >= '0' && d <= '7';
            });
            if (ok) {
                out.append(char(digits.toInt(&ok, 8)));
                i += 3;
                continue;
            }
        }
        out.append(c);
    }
    return QFile::decodeName(out);
}

QString nameOf(const QString &mountPoint)
{
    return QFileInfo(mountPoint).fileName();
}

bool sameDrives(const QList<Mount> &a, const QList<Mount> &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (a.at(i).device != b.at(i).device || a.at(i).mountPoint != b.at(i).mountPoint
            || a.at(i).fsType != b.at(i).fsType)
            return false;
    }
    return true;
}

} // namespace

DrivesModel::DrivesModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_poll(new QTimer(this))
{
    reload();
    m_poll->setInterval(pollMsecs);
    connect(m_poll, &QTimer::timeout, this, &DrivesModel::reload);
    m_poll->start();
}

DrivesModel::~DrivesModel()
{
    if (m_process) {
        // The window is going: udisksctl may finish on its own, but no one hears it any more.
        m_process->disconnect(this);
        m_process->waitForFinished(3000);
    }
}

QString DrivesModel::mountInfoPath()
{
    const QString given = qEnvironmentVariable("RILEY_MOUNTINFO");
    return given.isEmpty() ? QStringLiteral("/proc/self/mountinfo") : given;
}

QList<Mount> DrivesModel::parseMountInfo(const QByteArray &text)
{
    QList<Mount> mounts;
    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.trimmed().split(' ');
        // id parent major:minor root mount-point options [optional...] - type source super-options
        if (fields.size() < 9)
            continue;
        qsizetype dash = -1;
        for (qsizetype i = 6; i < fields.size(); ++i) {
            if (fields.at(i) == "-") {
                dash = i;
                break;
            }
        }
        if (dash < 0 || dash + 2 >= fields.size())
            continue;
        mounts.append({unescape(fields.at(dash + 2)), unescape(fields.at(4)), unescape(fields.at(dash + 1))});
    }
    return mounts;
}

bool DrivesModel::isUserMount(const QString &mountPoint)
{
    const QString clean = QDir::cleanPath(mountPoint);
    if (clean != mountPoint || !clean.startsWith(QLatin1Char('/')))
        return false;
    const QStringList parts = clean.mid(1).split(QLatin1Char('/'));
    if (parts.size() == 2 && (parts.at(0) == QLatin1String("media") || parts.at(0) == QLatin1String("mnt")))
        return !parts.at(1).isEmpty();
    if (parts.size() == 4 && parts.at(0) == QLatin1String("run") && parts.at(1) == QLatin1String("media"))
        return !parts.at(2).isEmpty() && !parts.at(3).isEmpty();
    return false;
}

QList<Mount> DrivesModel::userDrives(const QList<Mount> &mounts)
{
    // A later mount on the same point hides the one before (an automount placeholder first,
    // then the real file system), so the last line for a point is the one that counts.
    QList<Mount> drives;
    QHash<QString, qsizetype> at;
    for (const Mount &mount : mounts) {
        if (!isUserMount(mount.mountPoint))
            continue;
        if (const auto found = at.constFind(mount.mountPoint); found != at.constEnd()) {
            drives[*found] = mount;
        } else {
            at.insert(mount.mountPoint, drives.size());
            drives.append(mount);
        }
    }
    drives.removeIf([](const Mount &mount) { return mount.fsType == QLatin1String("autofs"); });
    std::sort(drives.begin(), drives.end(), [](const Mount &a, const Mount &b) {
        const QString an = nameOf(a.mountPoint), bn = nameOf(b.mountPoint);
        const int byCase = an.compare(bn, Qt::CaseInsensitive);
        if (byCase != 0)
            return byCase < 0;
        return a.mountPoint < b.mountPoint;
    });
    return drives;
}

void DrivesModel::reload()
{
    QFile file(mountInfoPath());
    if (!file.open(QIODevice::ReadOnly)) {
        setDrives({});
        return;
    }
    setDrives(userDrives(parseMountInfo(file.readAll())));
}

void DrivesModel::setDrives(const QList<Mount> &drives)
{
    if (sameDrives(drives, m_drives))
        return;
    const bool countChanges = drives.size() != m_drives.size();
    beginResetModel();
    m_drives = drives;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

int DrivesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_drives.size());
}

QVariant DrivesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_drives.size())
        return {};
    const Mount &drive = m_drives.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return nameOf(drive.mountPoint);
    case PathRole:
        return drive.mountPoint;
    case IconRole:
        return QStringLiteral("drive");
    case DeviceRole:
        return drive.device;
    default:
        return {};
    }
}

QHash<int, QByteArray> DrivesModel::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {IconRole, "icon"}, {DeviceRole, "device"}};
}

int DrivesModel::rowOf(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (qsizetype i = 0; i < m_drives.size(); ++i) {
        if (m_drives.at(i).mountPoint == clean)
            return int(i);
    }
    return -1;
}

int DrivesModel::rowHolding(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (qsizetype i = 0; i < m_drives.size(); ++i) {
        const QString &point = m_drives.at(i).mountPoint;
        if (clean == point || clean.startsWith(point + QLatin1Char('/')))
            return int(i);
    }
    return -1;
}

QString DrivesModel::pathAt(int row) const
{
    return row >= 0 && row < m_drives.size() ? m_drives.at(row).mountPoint : QString();
}

QString DrivesModel::nameAt(int row) const
{
    return row >= 0 && row < m_drives.size() ? nameOf(m_drives.at(row).mountPoint) : QString();
}

bool DrivesModel::eject(int row, QString *error)
{
    if (row < 0 || row >= m_drives.size()) {
        if (error)
            *error = QCoreApplication::translate("Drives", "That drive is not there any more.");
        return false;
    }
    const Mount drive = m_drives.at(row);
    const QString name = nameOf(drive.mountPoint);
    if (m_process) {
        if (error)
            *error = QCoreApplication::translate("Drives", "“%1” is still being ejected.").arg(m_ejectName);
        return false;
    }
    if (!drive.device.startsWith(QLatin1String("/dev/"))) {
        if (error)
            *error = QCoreApplication::translate("Drives", "“%1” is not a disk and cannot be ejected.").arg(name);
        return false;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("udisksctl")).isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("Drives", "Ejecting needs udisksctl (udisks2), which is not installed.");
        return false;
    }
    m_ejectDevice = drive.device;
    m_ejectName = name;
    runStep(QStringLiteral("unmount"));
    return true;
}

void DrivesModel::runStep(const QString &step)
{
    m_step = step;
    m_process = new QProcess(this);
    // The program and its arguments go straight to exec: no shell ever reads the device name.
    m_process->setProgram(QStandardPaths::findExecutable(QStringLiteral("udisksctl")));
    m_process->setArguments({step, QStringLiteral("-b"), m_ejectDevice});
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        stepFinished(exitCode, status != QProcess::NormalExit);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            stepFinished(-1, true);
    });
    m_process->start();
}

void DrivesModel::stepFinished(int exitCode, bool crashed)
{
    QProcess *process = m_process;
    if (!process)
        return;
    m_process = nullptr;
    process->disconnect(this);
    QString said = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    if (said.isEmpty())
        said = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
    process->deleteLater();

    const bool ok = !crashed && exitCode == 0;
    if (ok && m_step == QLatin1String("unmount")) {
        reload();
        runStep(QStringLiteral("power-off"));
        return;
    }
    if (said.isEmpty())
        said = crashed ? QCoreApplication::translate("Drives", "udisksctl did not run") : QCoreApplication::translate("Drives", "udisksctl ended with code %1").arg(exitCode);
    const QString name = m_ejectName;
    QString message;
    if (ok)
        message = QCoreApplication::translate("Drives", "“%1” can be removed now.").arg(name);
    else if (m_step == QLatin1String("unmount"))
        message = QCoreApplication::translate("Drives", "Cannot eject “%1”: %2").arg(name, said);
    else
        message = QCoreApplication::translate("Drives", "“%1” is unmounted, but it could not be powered off: %2").arg(name, said);
    m_ejectDevice.clear();
    m_ejectName.clear();
    reload();
    emit ejectFinished(name, ok, message);
}

} // namespace Riley
