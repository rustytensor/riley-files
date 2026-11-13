// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "customicons.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimeZone>

namespace Riley {

namespace {

QString clean(const QString &path)
{
    return QDir::cleanPath(path);
}

} // namespace

CustomIcons::CustomIcons(QObject *parent)
    : QObject(parent)
    , m_file(storeFile())
{
    refresh();
}

QString CustomIcons::storeFile()
{
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (!QDir::isAbsolutePath(data)) {
        const QString home = qEnvironmentVariable("HOME");
        data = (home.isEmpty() ? QDir::homePath() : home) + QStringLiteral("/.local/share");
    }
    return QDir::cleanPath(data) + QStringLiteral("/riley-files/icons.json");
}

void CustomIcons::refresh()
{
    const QFileInfo info(m_file);
    const QDateTime stamp = info.exists() ? info.lastModified(QTimeZone::UTC) : QDateTime();
    const qint64 size = info.exists() ? info.size() : -1;
    if (m_read && stamp == m_readStamp && size == m_readSize)
        return;
    m_read = true;
    m_readStamp = stamp;
    m_readSize = size;
    QHash<QString, QString> icons;
    QFile file(m_file);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString icon = it.value().toString();
            if (!icon.isEmpty())
                icons.insert(clean(it.key()), icon);
        }
    }
    if (icons != m_icons) {
        m_icons = icons;
        emit changed();
    }
}

bool CustomIcons::save()
{
    QJsonObject object;
    for (auto it = m_icons.cbegin(); it != m_icons.cend(); ++it)
        object.insert(it.key(), it.value());
    if (!QDir().mkpath(QFileInfo(m_file).absolutePath()))
        return false;
    QSaveFile out(m_file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!out.commit())
        return false;
    const QFileInfo info(m_file);
    m_readStamp = info.lastModified(QTimeZone::UTC);
    m_readSize = info.size();
    return true;
}

QString CustomIcons::icon(const QString &path)
{
    refresh();
    return m_icons.value(clean(path));
}

bool CustomIcons::set(const QString &path, const QString &icon)
{
    refresh();
    if (icon.isEmpty())
        return reset(path);
    m_icons.insert(clean(path), icon);
    const bool saved = save();
    emit changed();
    return saved;
}

bool CustomIcons::reset(const QString &path)
{
    refresh();
    if (!m_icons.remove(clean(path)))
        return true;
    const bool saved = save();
    emit changed();
    return saved;
}

bool CustomIcons::moved(const QString &from, const QString &to)
{
    refresh();
    const QString source = clean(from);
    const QString target = clean(to);
    const QString inside = source + u'/';
    QHash<QString, QString> icons;
    bool any = false;
    for (auto it = m_icons.cbegin(); it != m_icons.cend(); ++it) {
        if (it.key() == source) {
            icons.insert(target, it.value());
            any = true;
        } else if (it.key().startsWith(inside)) {
            icons.insert(target + it.key().mid(source.size()), it.value());
            any = true;
        } else if (!icons.contains(it.key())) {
            icons.insert(it.key(), it.value());
        }
    }
    if (!any)
        return true;
    m_icons = icons;
    const bool saved = save();
    emit changed();
    return saved;
}

} // namespace Riley
