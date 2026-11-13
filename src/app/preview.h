// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

// The preview overlay (Space): what is shown for the file under the cursor. A picture is shown
// whole, fitted to the window and never made larger; a text file shows its first lines; any
// other file shows its name and kind. The browser opens and closes it and moves it along with
// the cursor; QML only shows what this says.
class Preview : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the preview; QML only shows it.")

    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind NOTIFY changed)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(bool cut READ isCut NOTIFY changed)

public:
    enum Kind { Nothing, Image, Text, Other };
    Q_ENUM(Kind)

    // A text file shows at most this many lines, and reads at most this many bytes for them.
    static constexpr int maxLines = 200;
    static constexpr qint64 maxBytes = 256 * 1024;

    explicit Preview(QObject *parent = nullptr);

    bool isOpen() const { return m_open; }
    void setOpen(bool open);

    // Shows path (a file or folder; empty shows nothing).
    void show(const QString &path);

    QString path() const { return m_path; }
    QString name() const { return m_name; }
    // folder, image, text, ... (for its icon).
    QString category() const { return m_category; }
    Kind kind() const { return m_kind; }
    QUrl imageSource() const { return m_imageSource; }
    QString text() const { return m_text; }
    // True when the text shown is not the whole file.
    bool isCut() const { return m_cut; }

    // The first lines of a text file (at most maxLines), and whether there is more.
    static QString firstLines(const QString &path, bool *cut = nullptr);

signals:
    void openChanged();
    void changed();

private:
    bool m_open = false;
    QString m_path;
    QString m_name;
    QString m_category;
    Kind m_kind = Nothing;
    QUrl m_imageSource;
    QString m_text;
    bool m_cut = false;
};
