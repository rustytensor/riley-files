// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

namespace Riley {

// The icons a person chose for files and folders: a theme icon's name ("folder-music") or an
// image file's absolute path, by the item's absolute path. Kept by Riley Files itself in
// $XDG_DATA_HOME/riley-files/icons.json (a JSON object, path -> icon), so it needs no desktop
// service running. The file is read again whenever another window changed it.
//
// A rename or move made in Riley Files calls moved(), so the icon goes with the item (and with
// everything inside a moved folder).
class RILEYCORE_EXPORT CustomIcons : public QObject
{
    Q_OBJECT

public:
    explicit CustomIcons(QObject *parent = nullptr);

    // $XDG_DATA_HOME/riley-files/icons.json (or ~/.local/share/...), from the environment now.
    static QString storeFile();

    // The icon chosen for `path`, or empty.
    QString icon(const QString &path);
    bool has(const QString &path) { return !icon(path).isEmpty(); }
    // False when the store could not be written.
    bool set(const QString &path, const QString &icon);
    bool reset(const QString &path);
    // `from` now lives at `to`: its icon, and those of everything below it, follow.
    bool moved(const QString &from, const QString &to);

signals:
    // Some icon changed (here, or in another window's store read again).
    void changed();

private:
    void refresh();
    bool save();

    QString m_file;
    QHash<QString, QString> m_icons;
    QDateTime m_readStamp;
    qint64 m_readSize = -1;
    bool m_read = false;
};

} // namespace Riley
