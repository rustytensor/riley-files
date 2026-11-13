// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "networkplaces.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <unistd.h>

#include <algorithm>
#include <utility>

namespace Riley {

namespace {

constexpr int pollMs = 2000;

} // namespace

NetworkModel::NetworkModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_poll(new QTimer(this))
{
    reload();
    m_poll->setInterval(pollMs);
    connect(m_poll, &QTimer::timeout, this, &NetworkModel::reload);
    m_poll->start();
}

NetworkModel::~NetworkModel()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

int NetworkModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_places.size());
}

QVariant NetworkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_places.size())
        return {};
    const Place &place = m_places.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return place.name;
    case PathRole:
        return place.path;
    case IconRole:
        return QStringLiteral("network");
    default:
        return {};
    }
}

QHash<int, QByteArray> NetworkModel::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {IconRole, "icon"}};
}

int NetworkModel::rowOf(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (qsizetype i = 0; i < m_places.size(); ++i) {
        if (m_places.at(i).path == clean)
            return int(i);
    }
    return -1;
}

QString NetworkModel::pathAt(int row) const
{
    return row >= 0 && row < m_places.size() ? m_places.at(row).path : QString();
}

QString NetworkModel::nameAt(int row) const
{
    return row >= 0 && row < m_places.size() ? m_places.at(row).name : QString();
}

int NetworkModel::rowHolding(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    for (qsizetype i = 0; i < m_places.size(); ++i) {
        const QString &top = m_places.at(i).path;
        if (clean == top || clean.startsWith(top + QLatin1Char('/')))
            return int(i);
    }
    return -1;
}

QString NetworkModel::gvfsPath()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QStringLiteral("/run/user/%1").arg(::getuid());
    return QDir::cleanPath(runtime + QStringLiteral("/gvfs"));
}

QString NetworkModel::hostOf(const QString &folderName)
{
    const qsizetype colon = folderName.indexOf(QLatin1Char(':'));
    const QString rest = colon < 0 ? folderName : folderName.mid(colon + 1);
    // sftp, ftp and dav name it host=; smb names it server=.
    for (const char *key : {"host=", "server="}) {
        const QLatin1String prefix(key);
        for (const QString &part : rest.split(QLatin1Char(','))) {
            if (part.startsWith(prefix) && part.size() > prefix.size())
                return QUrl::fromPercentEncoding(part.mid(prefix.size()).toUtf8());
        }
    }
    return folderName;
}

bool NetworkModel::isConnectable(const QString &uri)
{
    static const QStringList schemes = {QStringLiteral("sftp"), QStringLiteral("smb"), QStringLiteral("ftp"),
                                        QStringLiteral("dav"), QStringLiteral("davs")};
    const QUrl url(uri.trimmed());
    return url.isValid() && schemes.contains(url.scheme().toLower()) && !url.host().isEmpty();
}

void NetworkModel::reload()
{
    const QDir dir(gvfsPath());
    QList<Place> places;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo &entry : entries)
        places.append({hostOf(entry.fileName()), QDir::cleanPath(entry.absoluteFilePath())});
    std::stable_sort(places.begin(), places.end(), [](const Place &a, const Place &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    setPlaces(places);
}

void NetworkModel::setPlaces(const QList<Place> &places)
{
    const bool same = places.size() == m_places.size()
                      && std::equal(places.cbegin(), places.cend(), m_places.cbegin(),
                                    [](const Place &a, const Place &b) { return a.name == b.name && a.path == b.path; });
    if (same)
        return;
    const bool countChanges = places.size() != m_places.size();
    beginResetModel();
    m_places = places;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

bool NetworkModel::connectTo(const QString &uri, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    const QString address = uri.trimmed();
    if (!isConnectable(address))
        return fail(QCoreApplication::translate("NetworkPlaces", "“%1” cannot be connected to. Give an sftp://, smb://, ftp://, dav:// or davs:// "
                                   "address with a server name.")
                        .arg(address));
    if (m_process)
        return fail(QCoreApplication::translate("NetworkPlaces", "A connection is being made already."));
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (gio.isEmpty())
        return fail(QCoreApplication::translate("NetworkPlaces", "Connecting to a server needs gio (glib2), which was not found."));

    m_uri = address;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        QProcess *process = std::exchange(m_process, nullptr);
        const QString said = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        process->deleteLater();
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        reload();
        emit connectFinished(m_uri, ok,
                             ok ? QCoreApplication::translate("NetworkPlaces", "Connected to %1.").arg(QUrl(m_uri).host())
                                : QCoreApplication::translate("NetworkPlaces", "Could not connect to %1: %2")
                                      .arg(m_uri, said.isEmpty() ? QCoreApplication::translate("NetworkPlaces", "gio mount failed.") : said));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart || !m_process)
            return;
        QProcess *process = std::exchange(m_process, nullptr);
        process->deleteLater();
        emit connectFinished(m_uri, false, QCoreApplication::translate("NetworkPlaces", "gio could not be started."));
    });
    // The address is one argument after "mount"; no shell ever reads it.
    m_process->start(gio, {QStringLiteral("mount"), address});
    return true;
}

} // namespace Riley
