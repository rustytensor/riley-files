// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "thumbnailer.h"

#include "installedthumbnailers.h"
#include "thumbnailcache.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>

namespace Riley {

namespace {

// A thumbnailer program gets this long before it is stopped and the file counted as failed.
constexpr int programTimeoutMs = 30000;

// The size that fits inside a bucket x bucket square, keeping the shape, never larger than the
// original. Rounded to the nearest pixel (600 x 400 in 256 gives 256 x 171).
QSize fitted(QSize original, int bucket)
{
    if (original.width() <= bucket && original.height() <= bucket)
        return original;
    const double scale = double(bucket) / std::max(original.width(), original.height());
    return QSize(std::max(1, int(std::lround(original.width() * scale))),
                 std::max(1, int(std::lround(original.height() * scale))));
}

QImage fittedImage(const QImage &image, int bucket)
{
    if (image.isNull())
        return image;
    const QSize size = fitted(image.size(), bucket);
    if (size == image.size())
        return image;
    return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

bool inside(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder + u'/');
}

} // namespace

Thumbnailer::Thumbnailer()
{
    m_pool.setMaxThreadCount(maxRunning());
}

Thumbnailer::~Thumbnailer()
{
    {
        QMutexLocker locker(&m_lock);
        m_stopping = true;
        m_pending.clear();
        m_live.clear();
    }
    m_pool.waitForDone();
}

quint64 Thumbnailer::request(const QString &path, int wanted, Done done)
{
    QMutexLocker locker(&m_lock);
    const quint64 ticket = m_nextTicket++;
    if (m_stopping)
        return ticket;
    m_pending.push_back(Job{ticket, path, wanted, std::move(done)});
    m_live.insert(ticket);
    startWorkers();
    return ticket;
}

void Thumbnailer::cancel(quint64 ticket)
{
    // Taken first, so a `done` already running for this ticket finishes before we return.
    QMutexLocker delivering(&m_delivering);
    QMutexLocker locker(&m_lock);
    m_live.remove(ticket);
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->ticket == ticket) {
            m_pending.erase(it);
            break;
        }
    }
}

void Thumbnailer::waitForDone()
{
    m_pool.waitForDone();
}

void Thumbnailer::startWorkers()
{
    while (m_workers < maxRunning() && m_workers < int(m_pending.size())) {
        ++m_workers;
        m_pool.start([this] { work(); });
    }
}

void Thumbnailer::work()
{
    for (;;) {
        Job job;
        {
            QMutexLocker locker(&m_lock);
            if (m_pending.empty() || m_stopping) {
                --m_workers;
                return;
            }
            job = std::move(m_pending.front());
            m_pending.pop_front();
        }

        const int now = ++m_running;
        for (int peak = m_peak.load(); now > peak && !m_peak.compare_exchange_weak(peak, now);) {
        }
        const Thumbnail result = make(job.path, job.wanted);
        --m_running;

        QMutexLocker delivering(&m_delivering);
        bool live = false;
        {
            QMutexLocker locker(&m_lock);
            live = m_live.remove(job.ticket);
        }
        if (live && job.done)
            job.done(result);
    }
}

const InstalledThumbnailers &Thumbnailer::installed()
{
    QMutexLocker locker(&m_installedLock);
    if (!m_installed)
        m_installed = std::make_unique<InstalledThumbnailers>();
    return *m_installed;
}

QSet<Qt::HANDLE> Thumbnailer::workThreads() const
{
    QMutexLocker locker(&m_threadsLock);
    return m_threads;
}

Thumbnail Thumbnailer::make(const QString &path, int wanted)
{
    {
        QMutexLocker locker(&m_threadsLock);
        m_threads.insert(QThread::currentThreadId());
    }
    Thumbnail result;
    result.path = path;
    const QFileInfo info(path);
    // Thumbnails of thumbnails would fill the cache with copies of itself.
    if (!info.isFile() || inside(info.absoluteFilePath(), ThumbnailCache::root()))
        return result;

    const QString target = ThumbnailCache::thumbnailPath(path, wanted);
    if (ThumbnailCache::isCurrent(target, path)) {
        result.image = QImage(target, "png");
        if (!result.image.isNull()) {
            result.status = Thumbnail::Status::Made;
            result.thumbnailFile = target;
            return result;
        }
    }
    const QString fail = ThumbnailCache::failPath(path);
    if (ThumbnailCache::isCurrent(fail, path)) {
        result.status = Thumbnail::Status::Failed;
        return result;
    }

    const int bucket = ThumbnailCache::bucketFor(wanted);
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(info);
    static const QList<QByteArray> readable = QImageReader::supportedMimeTypes();
    const bool image = readable.contains(mime.name().toLatin1());

    // The MIME type itself first, then its other names, then what it is a kind of.
    QStringList names{mime.name()};
    names += mime.aliases();
    names += mime.allAncestors();
    QString program;
    for (const QString &name : std::as_const(names)) {
        if (installed().forMimeType(name)) {
            program = name;
            break;
        }
    }
    if (!image && program.isEmpty())
        return result;

    ++m_attempts;
    QImage made;
    if (image)
        made = fromImageReader(path, bucket);
    if (made.isNull() && !program.isEmpty())
        made = fromInstalled(path, program, bucket);

    if (made.isNull()) {
        QImage mark(1, 1, QImage::Format_ARGB32);
        mark.fill(Qt::transparent);
        ThumbnailCache::store(mark, fail, path);
        result.status = Thumbnail::Status::Failed;
        return result;
    }
    result.status = Thumbnail::Status::Made;
    result.image = made;
    if (ThumbnailCache::store(made, target, path))
        result.thumbnailFile = target;
    return result;
}

QImage Thumbnailer::fromImageReader(const QString &path, int bucket) const
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
        reader.setScaledSize(fitted(size, bucket));
    // Scaled while decoding when the format can; any remainder is done here.
    return fittedImage(reader.read(), bucket);
}

QImage Thumbnailer::fromInstalled(const QString &path, const QString &mimeType, int bucket)
{
    const InstalledThumbnailers::Entry *entry = installed().forMimeType(mimeType);
    if (!entry)
        return {};
    const QTemporaryDir scratch;
    if (!scratch.isValid())
        return {};
    const QString output = scratch.filePath(QStringLiteral("thumbnail.png"));
    QStringList command = InstalledThumbnailers::command(entry->exec, QFileInfo(path).absoluteFilePath(),
                                                         output, bucket);
    if (command.isEmpty())
        return {};

    QProcess process;
    // What the program prints is of no use to anyone looking at a folder.
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setStandardInputFile(QProcess::nullDevice());
    const QString program = command.takeFirst();
    process.start(program, command);
    if (!process.waitForStarted(programTimeoutMs))
        return {};
    if (!process.waitForFinished(programTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return fittedImage(QImage(output), bucket);
}

} // namespace Riley
