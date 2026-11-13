// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Thumbnails in the main window, driven with keys through the real window. Expected results are
// the ones written down in the milestone's test list.

#include "browser.h"
#include "thumbnailer.h"
#include "uiharness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QCryptographicHash>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

class ThumbUiTests : public QObject
{
    Q_OBJECT

private slots:
    void thumbnailsOffTheUiThread();
    void uiGridShowsThumbnails();
};

void ThumbUiTests::thumbnailsOffTheUiThread()
{
    UiHarness ui;
    constexpr int images = 500;

    // One large picture, written 500 times under different names: each must be decoded and
    // scaled on its own, which takes far longer than a frame if it were done on the UI thread.
    QImage picture(1600, 1200, QImage::Format_RGB32);
    for (int y = 0; y < picture.height(); ++y)
        for (int x = 0; x < picture.width(); ++x)
            picture.setPixel(x, y, qRgb((x * 7 + y) % 256, (y * 3) % 256, (x ^ y) % 256));
    const QString source = QDir(ui.scratch()).filePath(u"picture.png"_s);
    QVERIFY(picture.save(source, "png"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray bytes = sourceFile.readAll();
    for (int i = 0; i < images; ++i) {
        QFile file(QDir(ui.root()).filePath(u"img%1.png"_s.arg(i, 3, 10, QLatin1Char('0'))));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }

    QVERIFY(ui.start({ui.root()}));
    QTRY_COMPARE(ui.browser().model()->count(), images);
    QTRY_VERIFY(!ui.browser().model()->isLoading());

    // Frames are asked for all the time; when the window stops drawing, the gap shows.
    QQuickWindow *w = ui.window();
    QElapsedTimer clock;
    QList<qint64> frames;
    const QMetaObject::Connection counting = QObject::connect(
        w, &QQuickWindow::frameSwapped, w, [&] { frames.append(clock.elapsed()); }, Qt::DirectConnection);
    QTimer ask;
    QObject::connect(&ask, &QTimer::timeout, w, &QQuickWindow::update);
    ask.start(10);
    clock.start();
    frames.append(0);

    // The grid, then down through every page of it, a page every 150 ms.
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QQuickItem *grid = ui.view(u"grid"_s);
    QVERIFY(grid);
    for (int page = 0; page < 60 && ui.browser().cursor() < images - 1; ++page) {
        ui.press(Qt::Key_PageDown);
        QTest::qWait(150);
    }
    QCOMPARE(ui.browser().cursor(), images - 1);

    // The last file's tile shows its thumbnail once it is made.
    const auto lastShown = [grid] {
        const QQuickItem *tile = UiHarness::delegateAt(grid, images - 1);
        if (!tile)
            return false;
        const auto children = tile->childItems();
        for (const QQuickItem *child : children) {
            if (child->objectName() == u"thumbnail"_s)
                return child->property("shown").toBool();
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(lastShown(), 20000);
    QTest::qWait(200);
    ask.stop();
    QObject::disconnect(counting);
    frames.append(clock.elapsed());

    qint64 gap = 0;
    for (qsizetype i = 1; i < frames.size(); ++i)
        gap = std::max(gap, frames.at(i) - frames.at(i - 1));
    const qsizetype made =
        QDir(QDir(ui.scratch()).filePath(u"cache/thumbnails/normal"_s)).entryList({u"*.png"_s}, QDir::Files).size();
    qInfo("%lld frames, longest gap %lld ms, %lld thumbnails made, at most %d at once",
          qint64(frames.size()), gap, qint64(made), ui.thumbnailer().peakRunning());

    // Thumbnails really were made while scrolling (at least a screenful), never more than four at once.
    const qsizetype perScreen = qsizetype(grid->width() / grid->property("cellWidth").toReal())
                                * qsizetype(grid->height() / grid->property("cellHeight").toReal());
    QVERIFY2(made >= perScreen, qPrintable(u"%1 thumbnails, a screen holds %2"_s.arg(made).arg(perScreen)));
    QVERIFY(ui.thumbnailer().peakRunning() >= 1);
    QVERIFY(ui.thumbnailer().peakRunning() <= Riley::Thumbnailer::maxRunning());
    QVERIFY2(frames.size() > 20, qPrintable(u"only %1 frames"_s.arg(frames.size())));
    QVERIFY2(gap <= 250, qPrintable(u"%1 ms without a frame"_s.arg(gap)));
    // And none of them was made on the UI thread, however short the work happened to be.
    QVERIFY(!ui.thumbnailer().workThreads().isEmpty());
    QVERIFY2(!ui.thumbnailer().workThreads().contains(QThread::currentThreadId()),
             "a thumbnail was made on the UI thread");

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

namespace {

// The child of a grid tile or list row with this objectName.
QQuickItem *childNamed(const QQuickItem *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    const auto children = parent->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == name)
            return child;
    }
    return nullptr;
}

} // namespace

void ThumbUiTests::uiGridShowsThumbnails()
{
    UiHarness ui;
    // A 64x48 picture (its thumbnail is never made larger, so it stays 64x48) and a text file.
    QImage picture(64, 48, QImage::Format_RGB32);
    picture.fill(qRgb(200, 60, 40));
    const QString png = QDir(ui.root()).filePath(u"p.png"_s);
    QVERIFY(picture.save(png, "png"));
    QVERIFY(ui.makeFile(u"q.txt"_s, 10));
    // Where the spec puts its thumbnail: normal/<md5 of its file:// URI>.png.
    const QString thumbFile = QDir(ui.scratch()).filePath(
        u"cache/thumbnails/normal/"_s
        + QString::fromLatin1(
            QCryptographicHash::hash(QUrl::fromLocalFile(png).toEncoded(), QCryptographicHash::Md5).toHex())
        + u".png"_s);

    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QQuickItem *grid = ui.view(u"grid"_s);
    QVERIFY(grid);
    QTRY_COMPARE(UiHarness::shownNames(grid), (QStringList{u"p.png"_s, u"q.txt"_s}));

    // The picture's tile shows its thumbnail in place of the icon, once made.
    const QQuickItem *tile = UiHarness::delegateAt(grid, 0);
    QQuickItem *thumbnail = childNamed(tile, u"thumbnail"_s);
    QVERIFY(thumbnail);
    QTRY_VERIFY_WITH_TIMEOUT(thumbnail->property("shown").toBool(), 10000);
    QVERIFY(thumbnail->isVisible());
    QCOMPARE(thumbnail->property("implicitWidth").toReal(), 64.0);
    QCOMPARE(thumbnail->property("implicitHeight").toReal(), 48.0);
    QVERIFY(QFileInfo::exists(thumbFile));
    QCOMPARE(QImage(thumbFile).size(), QSize(64, 48));

    // The text file has no thumbnail, and keeps its icon.
    const QQuickItem *textTile = UiHarness::delegateAt(grid, 1);
    QQuickItem *none = childNamed(textTile, u"thumbnail"_s);
    QVERIFY(none);
    QTest::qWait(300);
    QVERIFY(!none->property("shown").toBool());

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(ThumbUiTests)
#include "thumbui_tests.moc"
