// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

// What the details pane says about one file or folder: its name, kind, size (or how many items
// a folder holds), when it was modified, its permissions and, for a picture, its size in
// pixels. The browser points it at the row under the cursor; a folder's items are counted on a
// worker thread so a large folder never holds up the window.
class FileDetails : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the details; QML only shows them.")

    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    Q_PROPERTY(bool isDir READ isDir NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(QString modifiedText READ modifiedText NOTIFY changed)
    Q_PROPERTY(QString permissions READ permissions NOTIFY changed)
    Q_PROPERTY(QString imageSize READ imageSize NOTIFY changed)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY changed)

public:
    explicit FileDetails(QObject *parent = nullptr);
    ~FileDetails() override;

    // Reads what there is to say about path (empty: nothing).
    void setPath(const QString &path, bool countHidden = false);
    // Reads the same path again (it may have changed).
    void refresh();

    QString path() const { return m_path; }
    QString name() const { return m_name; }
    QString kind() const { return m_kind; }
    QString category() const { return m_category; }
    bool isDir() const { return m_isDir; }
    QString sizeText() const { return m_sizeText; }
    QString modifiedText() const { return m_modifiedText; }
    QString permissions() const { return m_permissions; }
    QString imageSize() const { return m_imageSize; }
    // The picture itself, for a picture the window can show; empty otherwise.
    QUrl imageSource() const { return m_imageSource; }

    // "300 bytes", "1 byte", "4.2 KB (4,200 bytes)".
    static QString sizeTextFor(qint64 bytes);
    // "3 items", "1 item", "No items".
    static QString itemsTextFor(qint64 items);
    // "rw-r--r--" for mode 0644.
    static QString permissionsTextFor(const QString &path);
    // The date and time as the pane shows it, in local time: "1 January 2020, 12:00".
    static QString dateTextFor(const QDateTime &when);
    // "image", "audio", "video", "text", "folder" ... from the MIME type (like the icons).
    static QString categoryFor(const QString &mimeName, bool isDir);

signals:
    void changed();

private:
    void countItems();

    QString m_path;
    bool m_countHidden = false;
    QString m_name;
    QString m_kind;
    QString m_category;
    bool m_isDir = false;
    QString m_sizeText;
    QString m_modifiedText;
    QString m_permissions;
    QString m_imageSize;
    QUrl m_imageSource;
    quint64 m_generation = 0; // the folder count that may still report back
};
