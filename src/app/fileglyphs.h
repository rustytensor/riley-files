// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// The file icons: nine drawings, one for each kind of thing (folder, image, audio, video, text,
// code, document, archive, other), kept as SVG files in the program (src/app/glyphs). Each is
// drawn in one colour, with a lighter tone where the drawing asks for one, at the size it is
// shown in device pixels, so it stays sharp at any scale. The colour comes from the Theme.
class FileGlyphs : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit FileGlyphs(QObject *parent = nullptr);

    // The nine kinds, in the order above.
    static QStringList kinds();
    // The kind drawn for a file category (FolderModel's "category"): the category itself when
    // it has a drawing, else "other" (a plain "file" among them).
    static QString kindFor(const QString &category);
    // The drawing of a kind in `colour`, `pixels` wide and high; a null image for an unknown kind
    // or when the SVG cannot be read.
    static QImage draw(const QString &kind, const QColor &colour, int pixels);

    // The Image source for a category's icon in `colour` (image://glyph/<kind>/<colour>).
    Q_INVOKABLE QString source(const QString &category, const QColor &colour) const;
};

// Serves FileGlyphs::draw() to QML Image items, at the size the Image asks for (its sourceSize in
// device pixels).
class GlyphProvider : public QQuickImageProvider
{
public:
    GlyphProvider();

    static QString id() { return QStringLiteral("glyph"); }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
