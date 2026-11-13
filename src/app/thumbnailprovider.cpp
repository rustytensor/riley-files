// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "thumbnailprovider.h"

#include "thumbnailer.h"

#include <QImage>
#include <QMetaObject>
#include <QQuickTextureFactory>

namespace {

// The tile size asked for when the Image gives none.
constexpr int defaultWanted = 128;

// One Image's request. It lives on the engine's image loading thread; the thumbnailer answers on
// one of its own threads and hands the image over through that thread's event queue, so the
// answer is dropped safely if the response is gone by then.
class ThumbnailResponse : public QQuickImageResponse
{
public:
    ThumbnailResponse(std::shared_ptr<Riley::Thumbnailer> thumbnailer, const QString &path, int wanted)
        : m_thumbnailer(std::move(thumbnailer))
    {
        m_ticket = m_thumbnailer->request(path, wanted, [this](const Riley::Thumbnail &thumbnail) {
            QImage image = thumbnail.image;
            QMetaObject::invokeMethod(
                this, [this, image] { finish(image); }, Qt::QueuedConnection);
        });
    }

    ~ThumbnailResponse() override { m_thumbnailer->cancel(m_ticket); }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return {}; }

    void cancel() override
    {
        m_thumbnailer->cancel(m_ticket);
        finish(QImage());
    }

private:
    void finish(const QImage &image)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_image = image;
        emit finished();
    }

    std::shared_ptr<Riley::Thumbnailer> m_thumbnailer;
    quint64 m_ticket = 0;
    QImage m_image;
    bool m_finished = false;
};

} // namespace

ThumbnailProvider::ThumbnailProvider(std::shared_ptr<Riley::Thumbnailer> thumbnailer)
    : m_thumbnailer(std::move(thumbnailer))
{
}

QString ThumbnailProvider::sourceFor(const QString &path, const QDateTime &modified)
{
    // The path travels as base64url, so no character of a file name can be read as part of the
    // address.
    const QByteArray encoded =
        path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("image://") + id() + u'/' + QString::fromLatin1(encoded) + u'/'
           + QString::number(modified.isValid() ? modified.toMSecsSinceEpoch() : 0);
}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const QString encoded = id.section(u'/', 0, 0);
    const QString path = QString::fromUtf8(QByteArray::fromBase64(
        encoded.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    int wanted = std::max(requestedSize.width(), requestedSize.height());
    if (wanted <= 0)
        wanted = defaultWanted;
    return new ThumbnailResponse(m_thumbnailer, path, wanted);
}
