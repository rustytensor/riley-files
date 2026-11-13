// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "fileglyphs.h"

#include <QFile>
#include <QImageReader>
#include <QPainter>

#include <algorithm>

namespace {

// The size drawn when the Image asks for none.
constexpr int defaultPixels = 32;
// The largest size drawn, whatever is asked.
constexpr int largestPixels = 1024;

} // namespace

FileGlyphs::FileGlyphs(QObject *parent)
    : QObject(parent)
{
}

QStringList FileGlyphs::kinds()
{
    static const QStringList all = {
        QStringLiteral("folder"), QStringLiteral("image"),    QStringLiteral("audio"),
        QStringLiteral("video"),  QStringLiteral("text"),     QStringLiteral("code"),
        QStringLiteral("document"), QStringLiteral("archive"), QStringLiteral("other"),
    };
    return all;
}

QString FileGlyphs::kindFor(const QString &category)
{
    return kinds().contains(category) ? category : QStringLiteral("other");
}

QImage FileGlyphs::draw(const QString &kind, const QColor &colour, int pixels)
{
    if (!kinds().contains(kind) || pixels <= 0)
        return {};
    pixels = std::min(pixels, largestPixels);
    QFile file(QStringLiteral(":/glyphs/%1.svg").arg(kind));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&file, "svg");
    reader.setScaledSize(QSize(pixels, pixels));
    QImage shape = reader.read();
    if (shape.isNull())
        return {};
    // The drawing gives the shape and its tones (its alpha); the colour is laid into it.
    QImage glyph = shape.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&glyph);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(glyph.rect(), colour);
    painter.end();
    return glyph;
}

QString FileGlyphs::source(const QString &category, const QColor &colour) const
{
    return QStringLiteral("image://%1/%2/%3")
        .arg(GlyphProvider::id(), kindFor(category), colour.name(QColor::HexArgb).mid(1));
}

GlyphProvider::GlyphProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage GlyphProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // "<kind>/<aarrggbb>"
    const QString kind = id.section(QLatin1Char('/'), 0, 0);
    const QColor colour = QColor::fromString(QLatin1Char('#') + id.section(QLatin1Char('/'), 1, 1));
    int pixels = std::max(requestedSize.width(), requestedSize.height());
    if (pixels <= 0)
        pixels = defaultPixels;
    const QImage glyph = FileGlyphs::draw(kind, colour.isValid() ? colour : QColor(Qt::gray), pixels);
    if (size)
        *size = glyph.size();
    return glyph;
}
