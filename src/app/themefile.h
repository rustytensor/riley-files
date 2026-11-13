// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// The Riley desktop's active theme, as Theme.qml reads it. Riley keeps it at
// $XDG_CONFIG_HOME/riley/current-theme, a link to a theme folder holding colors.toml:
//
//     mode = "dark"
//     accent = "#f59e42"
//     background = "#17130f"
//
// Riley switches themes by making a new link beside the old one and renaming it over it, so the
// old colors.toml never changes. The link's folder, the theme folder and the file are all
// watched, and the colours are read again within a moment of any change: a switch or an edit
// shows without a restart. With no theme, colors is empty and Theme.qml uses its own palette.
class ThemeFile : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Riley's colour names (accent, background, ...) to "#rrggbb" values; only valid colours.
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    // "light" or "dark": "dark" when the theme does not say.
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    // Whether a colors.toml was found and read.
    Q_PROPERTY(bool found READ found NOTIFY changed)
    // The colors.toml read (through the link), or the one that would be.
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    explicit ThemeFile(QObject *parent = nullptr);

    QVariantMap colors() const { return m_colors; }
    QString mode() const { return m_mode; }
    bool found() const { return m_found; }
    QString path() const;

    // Reads the theme again now (also done by itself after any change on disk).
    Q_INVOKABLE void reload();

    // The 'key = "value"' lines of a small TOML file, keys in lower case. Comments, tables and
    // lines of any other shape are skipped. Exposed for the tests.
    static QVariantMap readKeys(const QString &file);

signals:
    void changed();

private:
    void watchAgain();

    QString m_folder;   // $XDG_CONFIG_HOME/riley
    QString m_link;     // m_folder + "/current-theme"
    QVariantMap m_colors;
    QString m_mode = QStringLiteral("dark");
    bool m_found = false;
    QFileSystemWatcher m_watcher;
    // Several changes in a row (a link renamed, a file rewritten) give one read.
    QTimer m_settle;
};
