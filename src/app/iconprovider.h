// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QHash>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

#include <memory>

// The icons of the installed icon themes (freedesktop Icon Theme specification), found by
// walking the icons folders ($HOME/.icons, $XDG_DATA_HOME/icons, each $XDG_DATA_DIRS/icons, and
// the pixmaps folders) once, on first use, from any thread. A name found in several themes is
// taken from the theme in use, else hicolor, else any; a scalable image before the largest one.
class IconIndex
{
public:
    // The index, built on the first call (which waits for it).
    static std::shared_ptr<const IconIndex> get();

    // Every icon name, sorted.
    QStringList names() const { return m_names; }
    // The image file of an icon name, or empty.
    QString fileFor(const QString &name) const { return m_files.value(name); }

    // The theme in use, as the program found it when it started (set from the UI thread).
    static void setThemeName(const QString &name);

private:
    IconIndex();

    QStringList m_names;
    QHash<QString, QString> m_files;
};

// Serves the icons a person chose for files (Change Icon) to QML Image items, as
// image://icon/<icon>: a theme icon's name, or an image file's absolute path. sourceFor() makes
// the address, so QML never builds one itself.
class IconProvider : public QQuickImageProvider
{
public:
    IconProvider();

    static QString id() { return QStringLiteral("icon"); }
    static QString sourceFor(const QString &icon);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
