// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "versionmodel.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

namespace Riley {

VersionModel::VersionModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_wanted(std::make_shared<std::atomic<quint64>>(0))
{
    m_pool.setMaxThreadCount(1);
}

VersionModel::~VersionModel()
{
    m_wanted->store(quint64(-1));
    m_pool.waitForDone();
}

int VersionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_versions.size());
}

QString VersionModel::dateText(const QDateTime &date)
{
    return date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QVariant VersionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_versions.size())
        return {};
    const FileVersion &v = m_versions.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DateTextRole: return dateText(v.snapshot.date);
    case NumberRole: return v.snapshot.number;
    case DateRole: return v.snapshot.date;
    case DescriptionRole: return v.snapshot.description;
    case CopyPathRole: return v.path;
    case IsDirRole: return v.isFolder;
    case SizeRole: return v.size;
    case ModifiedRole: return v.modified;
    default: return {};
    }
}

QHash<int, QByteArray> VersionModel::roleNames() const
{
    return {
        {NumberRole, "number"},
        {DateRole, "date"},
        {DateTextRole, "dateText"},
        {DescriptionRole, "description"},
        {CopyPathRole, "copyPath"},
        {IsDirRole, "isDir"},
        {SizeRole, "size"},
        {ModifiedRole, "modified"},
    };
}

void VersionModel::setPath(const QString &path)
{
    if (path == m_path)
        return;
    m_path = path;
    emit pathChanged();
    start();
}

void VersionModel::reload()
{
    start();
}

void VersionModel::start()
{
    const quint64 generation = ++m_generation;
    m_wanted->store(generation);
    if (!m_versions.isEmpty()) {
        beginResetModel();
        m_versions.clear();
        endResetModel();
        emit countChanged();
    }
    if (m_path.isEmpty() || Snapshots::isInSnapshot(m_path)) {
        if (m_available) {
            m_available = false;
            emit availableChanged();
        }
        if (m_loading) {
            m_loading = false;
            emit loadingChanged();
        }
        emit loaded();
        return;
    }
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    const QString path = m_path;
    const std::shared_ptr<std::atomic<quint64>> wanted = m_wanted;
    m_pool.start([this, path, generation, wanted] {
        {
            QMutexLocker lock(&m_threadsLock);
            m_threads.insert(QThread::currentThreadId());
        }
        auto stop = [wanted, generation] { return wanted->load() != generation; };
        if (stop())
            return;
        const bool available = !Snapshots::baseFor(path).isEmpty();
        const QList<FileVersion> versions = available ? Snapshots::versionsOf(path, stop) : QList<FileVersion>();
        if (stop())
            return;
        QMetaObject::invokeMethod(this, [this, generation, available, versions] {
            apply(generation, available, versions);
        }, Qt::QueuedConnection);
    });
}

void VersionModel::apply(quint64 generation, bool available, const QList<FileVersion> &versions)
{
    if (generation != m_generation)
        return;
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
    beginResetModel();
    m_versions = versions;
    endResetModel();
    emit countChanged();
    m_loading = false;
    emit loadingChanged();
    emit loaded();
}

QVariantMap VersionModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_versions.size())
        return map;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

QString VersionModel::copyPathAt(int row) const
{
    return row >= 0 && row < m_versions.size() ? m_versions.at(row).path : QString();
}

QDateTime VersionModel::dateAt(int row) const
{
    return row >= 0 && row < m_versions.size() ? m_versions.at(row).snapshot.date : QDateTime();
}

QString VersionModel::dateTextAt(int row) const
{
    return row >= 0 && row < m_versions.size() ? dateText(m_versions.at(row).snapshot.date) : QString();
}

QSet<Qt::HANDLE> VersionModel::readingThreads() const
{
    QMutexLocker lock(&m_threadsLock);
    return m_threads;
}

} // namespace Riley
