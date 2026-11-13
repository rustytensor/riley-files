// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The file icons: nine drawings from the program's own SVG files, one for each kind (folder, image,
// audio, video, text, code, document, archive, other), told apart by their shapes and not only by
// their colours, used for the files in every view, and drawn from the SVG at the screen's pixels
// at twice the scale rather than stretched. Expected results are the ones written down in the
// milestone's test list.

#include "browser.h"
#include "fileglyphs.h"
#include "uiharness.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace {

// The fixture: one item of each kind, in the order the list shows them (folders first).
struct Sample
{
    QString name;
    QString kind;
};

const QList<Sample> &samples()
{
    static const QList<Sample> all = {
        {u"dir"_s, u"folder"_s},    {u"a.png"_s, u"image"_s},    {u"b.mp3"_s, u"audio"_s},
        {u"c.mp4"_s, u"video"_s},   {u"d.txt"_s, u"text"_s},     {u"e.cpp"_s, u"code"_s},
        {u"f.pdf"_s, u"document"_s}, {u"g.zip"_s, u"archive"_s}, {u"h.bin"_s, u"other"_s},
    };
    return all;
}

bool makeSamples(const UiHarness &ui)
{
    for (const Sample &sample : samples()) {
        if (sample.kind == u"folder") {
            if (!QDir().mkpath(ui.root() + u'/' + sample.name))
                return false;
        } else if (!ui.makeFile(sample.name, 100)) {
            return false;
        }
    }
    return true;
}

QQuickItem *findIn(QQuickItem *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    const auto children = parent->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findIn(child, name))
            return found;
    }
    return nullptr;
}

// The drawing of the icon on a view's row (the Image inside its FileIcon).
QQuickItem *glyphOn(QQuickItem *view, int row)
{
    return findIn(findIn(UiHarness::delegateAt(view, row), u"fileIcon"_s), u"glyph"_s);
}

QString kindOf(QQuickItem *glyph)
{
    // image://glyph/<kind>/<colour>
    const QString source = glyph ? glyph->property("source").toUrl().toString() : QString();
    return source.startsWith(u"image://glyph/"_s) ? source.section(u'/', 3, 3) : QString();
}

// Opaque enough to count as part of the shape.
QList<bool> maskOf(const QImage &image)
{
    QList<bool> mask;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        for (int x = 0; x < argb.width(); ++x)
            mask.append(qAlpha(argb.pixel(x, y)) >= 128);
    }
    return mask;
}

// How many pixels of a picture of the icon over `background` lie between the background and the
// icon's colour: its soft edges. A drawing stretched to twice its size has edges twice as wide.
int softPixels(const QImage &picture, QColor background, QColor ink)
{
    const QImage rgb = picture.convertToFormat(QImage::Format_RGB32);
    const double br = background.redF(), bg = background.greenF(), bb = background.blueF();
    const double dr = ink.redF() - br, dg = ink.greenF() - bg, db = ink.blueF() - bb;
    const double length = dr * dr + dg * dg + db * db;
    if (length < 0.01)
        return -1;
    int soft = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QColor c = QColor::fromRgb(rgb.pixel(x, y));
            const double t = ((c.redF() - br) * dr + (c.greenF() - bg) * dg + (c.blueF() - bb) * db) / length;
            if (t > 0.15 && t < 0.85)
                ++soft;
        }
    }
    return soft;
}

QImage over(const QImage &glyph, QColor background)
{
    QImage picture(glyph.size(), QImage::Format_RGB32);
    picture.fill(background);
    QPainter painter(&picture);
    painter.drawImage(0, 0, glyph);
    painter.end();
    return picture;
}

// The child run of fileIconsByKind (RILEY_ICONS set, with QT_SCALE_FACTOR=2): for each kind, the
// icon as the window draws it, and the same drawing made from the SVG at the screen's pixels and
// stretched from half of them, all over the row's background. Prints "<kind> grab=<n>
// native=<n> stretched=<n>" (their soft pixels), then "warnings=<n>".
int iconsChild()
{
    UiHarness ui;
    if (!makeSamples(ui) || !ui.start({ui.root()}))
        return 1;
    QQuickWindow *w = ui.window();
    QQuickItem *list = ui.view(u"list"_s);
    if (!QTest::qWaitFor([&] { return list && list->property("count").toInt() == int(samples().size()); }))
        return 2;
    // Off the first row, so no row has the cursor's colour behind its icon only in part.
    ui.press(Qt::Key_Escape);
    QTest::qWait(300);
    const qreal ratio = w->devicePixelRatio();
    const QImage grab = w->grabWindow();
    for (int row = 0; row < int(samples().size()); ++row) {
        QQuickItem *glyph = glyphOn(list, row);
        if (!glyph)
            return 3;
        const QPointF at = glyph->mapToScene(QPointF(0, 0)) * ratio;
        const int pixels = qRound(glyph->width() * ratio);
        const QImage seen = grab.copy(qRound(at.x()), qRound(at.y()), pixels, pixels);
        const QColor background = seen.pixelColor(0, 0);
        const QColor ink = glyph->parentItem()->property("colour").value<QColor>();
        const QString kind = samples().at(row).kind;
        const QImage native = FileGlyphs::draw(kind, ink, pixels);
        const QImage stretched = FileGlyphs::draw(kind, ink, pixels / 2)
                                     .scaled(pixels, pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        std::printf("%s grab=%d native=%d stretched=%d\n", qPrintable(kind), softPixels(seen, background, ink),
                    softPixels(over(native, background), background, ink),
                    softPixels(over(stretched, background), background, ink));
    }
    std::printf("warnings=%lld\n", static_cast<long long>(ui.qmlWarnings().size()));
    std::fflush(stdout);
    return 0;
}

} // namespace

class IconUiTests : public QObject
{
    Q_OBJECT

private slots:
    void fileIconsByKind();
};

void IconUiTests::fileIconsByKind()
{
    // Nine drawings, each an SVG file of the program's own.
    const QStringList kinds = FileGlyphs::kinds();
    QCOMPARE(kinds, (QStringList{u"folder"_s, u"image"_s, u"audio"_s, u"video"_s, u"text"_s, u"code"_s,
                                 u"document"_s, u"archive"_s, u"other"_s}));
    QList<QList<bool>> masks;
    for (const QString &kind : kinds) {
        QFile svg(u":/glyphs/"_s + kind + u".svg"_s);
        QVERIFY2(svg.open(QIODevice::ReadOnly), qPrintable(kind));
        QVERIFY2(svg.readAll().contains("<svg"), qPrintable(kind));
        const QImage glyph = FileGlyphs::draw(kind, QColor(0x33, 0x66, 0x99), 32);
        QVERIFY2(glyph.size() == QSize(32, 32), qPrintable(kind));
        masks.append(maskOf(glyph));
    }

    // Different shapes, not one shape in different colours: every pair's masks differ in at
    // least 10% of their pixels.
    for (int a = 0; a < masks.size(); ++a) {
        for (int b = a + 1; b < masks.size(); ++b) {
            int differ = 0;
            for (int i = 0; i < masks.at(a).size(); ++i)
                differ += masks.at(a).at(i) != masks.at(b).at(i);
            QVERIFY2(differ * 10 >= masks.at(a).size(),
                     qPrintable(kinds.at(a) + u" and "_s + kinds.at(b) + u": "_s + QString::number(differ)));
        }
    }

    // In the window, every view gives each file the drawing of its kind.
    UiHarness ui;
    QVERIFY(makeSamples(ui));
    QVERIFY(ui.start({ui.root()}));
    QStringList expected;
    for (const Sample &sample : samples())
        expected.append(sample.kind);
    auto kindsIn = [](QQuickItem *view) {
        QStringList found;
        const int count = view ? view->property("count").toInt() : 0;
        for (int row = 0; row < count; ++row)
            found.append(kindOf(glyphOn(view, row)));
        return found;
    };
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"list"_s));
    QTRY_COMPARE(kindsIn(ui.view(u"list"_s)), expected);
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"grid"_s));
    QTRY_COMPARE(kindsIn(ui.view(u"grid"_s)), expected);
    ui.press(Qt::Key_3, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"column1"_s));
    QTRY_COMPARE(kindsIn(ui.view(u"column1"_s)), expected);
    // The drawing is asked for at the size it is shown.
    QQuickItem *glyph = glyphOn(ui.view(u"column1"_s), 0);
    QVERIFY(glyph);
    QCOMPARE(glyph->property("sourceSize").toSize(), glyph->size().toSize());
    QCOMPARE(ui.qmlWarnings(), QStringList());
    ui.stop();

    // At twice the scale, the window shows each icon drawn from the SVG at its own pixels: its
    // soft edges are those of the drawing made at that size, not of one stretched from half.
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"RILEY_ICONS"_s, u"1"_s);
    env.insert(u"QT_SCALE_FACTOR"_s, u"2"_s);
    env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), {});
    QVERIFY(child.waitForFinished(60000));
    const QString out = QString::fromLocal8Bit(child.readAllStandardOutput());
    const QString err = QString::fromLocal8Bit(child.readAllStandardError());
    QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, qPrintable(out + err));
    QVERIFY2(out.contains(u"warnings=0"_s), qPrintable(out + err));
    for (const QString &kind : kinds) {
        const QRegularExpression line(u"^"_s + kind + u" grab=(-?\\d+) native=(-?\\d+) stretched=(-?\\d+)$"_s,
                                      QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = line.match(out);
        QVERIFY2(match.hasMatch(), qPrintable(kind + u": "_s + out));
        const int grab = match.captured(1).toInt();
        const int native = match.captured(2).toInt();
        const int stretched = match.captured(3).toInt();
        QVERIFY2(native > 0 && stretched > native, qPrintable(match.captured(0)));
        QVERIFY2(grab >= 0 && grab * 2 <= native + stretched, qPrintable(match.captured(0)));
    }
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("RILEY_ICONS"))
        return iconsChild();
    IconUiTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}

#include "iconui_tests.moc"
