// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QDateTime>
#include <QQuickAsyncImageProvider>
#include <QString>

#include <memory>

namespace Riley {
class Thumbnailer;
}

// Serves thumbnails to QML Image items as image://thumbnail/... The work is done by a
// Riley::Thumbnailer on its worker threads; an Image whose tile leaves the screen drops its
// request before it starts. sourceFor() makes the address, so QML never builds one itself.
class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    explicit ThumbnailProvider(std::shared_ptr<Riley::Thumbnailer> thumbnailer);

    // The name the engine knows the provider by.
    static QString id() { return QStringLiteral("thumbnail"); }

    // The Image source for the thumbnail of `path`. The modified time is part of it, so a file
    // that changes is asked for again rather than taken from the engine's own image cache.
    static QString sourceFor(const QString &path, const QDateTime &modified);

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    std::shared_ptr<Riley::Thumbnailer> m_thumbnailer;
};
