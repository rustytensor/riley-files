// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Thumbnails on real files: the freedesktop cache layout and keys, reuse, remaking after a
// change, fail entries, and installed .thumbnailer programs.

#include "thumbnailer.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <optional>

using namespace Qt::StringLiterals;
using Riley::Thumbnail;
using Riley::Thumbnailer;

namespace {

// Asks once through the worker threads and waits for the answer.
Thumbnail ask(Thumbnailer &thumbnailer, const QString &path, int wanted)
{
    QMutex lock;
    std::optional<Thumbnail> answer;
    thumbnailer.request(path, wanted, [&](const Thumbnail &t) {
        QMutexLocker locker(&lock);
        answer = t;
    });
    thumbnailer.waitForDone();
    QMutexLocker locker(&lock);
    return answer.value_or(Thumbnail{});
}

// The cache name worked out here from the spec, not by the code under test.
QString md5Name(const QString &path)
{
    const QByteArray uri = "file://" + path.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex()) + u".png"_s;
}

bool setModified(const QString &path, const QDateTime &when)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite) && file.setFileTime(when, QFileDevice::FileModificationTime);
}

QDateTime fileModified(const QString &path)
{
    return QFileInfo(path).lastModified(QTimeZone::UTC);
}

bool writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

} // namespace

class ThumbsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void thumbnailsFollowTheSpec();
    void thumbnailsUseInstalledThumbnailers();

private:
    QTemporaryDir m_dir;
};

void ThumbsTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // Everything the thumbnailer reads and writes stays in the scratch folder. The system's MIME
    // database is kept; the test's own MIME type comes from the user data folder.
    qputenv("XDG_CACHE_HOME", m_dir.filePath(u"cache"_s).toUtf8());
    qputenv("XDG_DATA_HOME", m_dir.filePath(u"data-home"_s).toUtf8());
    qputenv("XDG_DATA_DIRS", (m_dir.filePath(u"data"_s) + u":/usr/local/share:/usr/share"_s).toUtf8());

    QVERIFY(QDir().mkpath(m_dir.filePath(u"data-home/mime/packages"_s)));
    QVERIFY(writeText(m_dir.filePath(u"data-home/mime/packages/riley-test.xml"_s),
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                      "  <mime-type type=\"text/x-riley-test\">\n"
                      "    <comment>Riley test file</comment>\n"
                      "    <sub-class-of type=\"text/plain\"/>\n"
                      "    <glob pattern=\"*.rileytest\"/>\n"
                      "  </mime-type>\n"
                      "</mime-info>\n"));
}

void ThumbsTests::thumbnailsFollowTheSpec()
{
    const QString photo = m_dir.filePath(u"photo.png"_s);
    QImage image(600, 400, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x % 256, y % 256, 120));
    QVERIFY(image.save(photo, "png"));
    QVERIFY(setModified(photo, QDateTime(QDate(2021, 3, 4), QTime(5, 6, 7), QTimeZone::UTC)));

    const QString large = m_dir.filePath(u"cache/thumbnails/large/"_s) + md5Name(photo);
    Thumbnailer thumbnailer;

    // Made: in large/, 256 x 171, with the spec's keys.
    Thumbnail made = ask(thumbnailer, photo, 256);
    QCOMPARE(made.status, Thumbnail::Status::Made);
    QVERIFY2(QFileInfo::exists(large), qPrintable(large));
    QCOMPARE(made.thumbnailFile, large);
    {
        // Read with QImage: QImageReader::text() cuts keys at their first colon.
        const QImage stored(large);
        QCOMPARE(stored.text(u"Thumb::URI"_s), u"file://"_s + photo);
        QCOMPARE(stored.text(u"Thumb::MTime"_s), u"1614834367"_s);
        QCOMPARE(stored.size(), QSize(256, 171));
    }
    QCOMPARE(thumbnailer.attempts(), 1);

    // Asked again: the cached file is used, not made again.
    const QDateTime old(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC);
    QVERIFY(setModified(large, old));
    made = ask(thumbnailer, photo, 256);
    QCOMPARE(made.status, Thumbnail::Status::Made);
    QCOMPARE(made.image.size(), QSize(256, 171));
    QCOMPARE(fileModified(large), old);
    QCOMPARE(thumbnailer.attempts(), 1);

    // The source changes: made again, with the new time.
    QImage changed(600, 400, QImage::Format_RGB32);
    changed.fill(Qt::darkGreen);
    QVERIFY(changed.save(photo, "png"));
    QVERIFY(setModified(photo, QDateTime(QDate(2022, 6, 7), QTime(8, 9, 10), QTimeZone::UTC)));
    made = ask(thumbnailer, photo, 256);
    QCOMPARE(made.status, Thumbnail::Status::Made);
    QCOMPARE(thumbnailer.attempts(), 2);
    QVERIFY(fileModified(large) != old);
    QCOMPARE(QImage(large).text(u"Thumb::MTime"_s), u"1654589350"_s);
    QCOMPARE(QImage(large).pixel(100, 100), QColor(Qt::darkGreen).rgb());

    // A broken image: a fail entry, and no second try.
    const QString broken = m_dir.filePath(u"broken.png"_s);
    QVERIFY(writeText(broken, "\x89PNG\r\n\x1a\n this is not an image"));
    Thumbnail failed = ask(thumbnailer, broken, 256);
    QCOMPARE(failed.status, Thumbnail::Status::Failed);
    const QString failEntry = m_dir.filePath(u"cache/thumbnails/fail/riley-files/"_s) + md5Name(broken);
    QVERIFY2(QFileInfo::exists(failEntry), qPrintable(failEntry));
    QCOMPARE(QImage(failEntry).text(u"Thumb::URI"_s), u"file://"_s + broken);
    QCOMPARE(thumbnailer.attempts(), 3);
    failed = ask(thumbnailer, broken, 256);
    QCOMPARE(failed.status, Thumbnail::Status::Failed);
    QCOMPARE(thumbnailer.attempts(), 3);
    QVERIFY(!QFileInfo::exists(m_dir.filePath(u"cache/thumbnails/large/"_s) + md5Name(broken)));
}

void ThumbsTests::thumbnailsUseInstalledThumbnailers()
{
    // The picture the program "makes": 100 x 50, red.
    const QString picture = m_dir.filePath(u"made-by-program.png"_s);
    QImage red(100, 50, QImage::Format_RGB32);
    red.fill(Qt::red);
    QVERIFY(red.save(picture, "png"));

    const QString script = m_dir.filePath(u"make-thumbnail.sh"_s);
    const QString argsFile = m_dir.filePath(u"args.txt"_s);
    QVERIFY(writeText(script, ("#!/bin/sh\necho \"$1 $3\" > '" + argsFile.toUtf8() + "'\ncp '"
                               + picture.toUtf8() + "' \"$2\"\n")));
    QVERIFY(QDir().mkpath(m_dir.filePath(u"data/thumbnailers"_s)));
    QVERIFY(writeText(m_dir.filePath(u"data/thumbnailers/riley-test.thumbnailer"_s),
                      "[Thumbnailer Entry]\nTryExec=/bin/sh\nExec=/bin/sh " + script.toUtf8()
                          + " %i %o %s\nMimeType=text/x-riley-test;\n"));

    const QString note = m_dir.filePath(u"note.rileytest"_s);
    QVERIFY(writeText(note, "hello\n"));

    Thumbnailer thumbnailer;
    const Thumbnail made = ask(thumbnailer, note, 128);
    QCOMPARE(made.status, Thumbnail::Status::Made);

    QFile args(argsFile);
    QVERIFY(args.open(QIODevice::ReadOnly));
    QCOMPARE(args.readAll().trimmed(), (note + u" 128"_s).toUtf8());

    const QString normal = m_dir.filePath(u"cache/thumbnails/normal/"_s) + md5Name(note);
    QVERIFY2(QFileInfo::exists(normal), qPrintable(normal));
    const QImage stored(normal);
    QCOMPARE(stored.size(), QSize(100, 50));
    QCOMPARE(stored.pixel(10, 10), QColor(Qt::red).rgb());
    QCOMPARE(QImage(normal).text(u"Thumb::URI"_s), u"file://"_s + note);
}

QTEST_GUILESS_MAIN(ThumbsTests)
#include "thumbs_tests.moc"
