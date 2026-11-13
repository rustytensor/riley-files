// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The look of the window: the colours following the Riley desktop's theme as it switches, no
// colour anywhere but in Theme.qml, the window at twice the scale, how fast it starts, and
// pictures of the main views in both variants for a person to look at.

#include "browser.h"
#include "foldermodel.h"
#include "trash.h"
#include "uiharness.h"

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace {

// Riley's own palette, as Theme.qml writes it.
constexpr auto builtinDarkBackground = "#1c1916";
constexpr auto builtinDarkAccent = "#e0894a";
constexpr auto builtinLightBackground = "#f7f2eb";

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QObject *themeOf(const UiHarness &ui)
{
    QQmlEngine *engine = qmlEngine(ui.window());
    return engine ? engine->singletonInstance<QObject *>("RileyFiles", "Theme") : nullptr;
}

QString colourOf(QObject *theme, const char *role)
{
    return theme ? theme->property(role).value<QColor>().name() : QString();
}

// Makes the theme Riley would: <riley>/themes/<name>/colors.toml, and current-theme pointing at
// it, switched the way Riley switches (a new link renamed over the old one).
bool makeTheme(const QString &riley, const QString &name, const QByteArray &colours)
{
    return writeFile(riley + u"/themes/"_s + name + u"/colors.toml"_s, colours);
}

bool switchTheme(const QString &riley, const QString &name)
{
    const QString next = riley + u"/current-theme.next"_s;
    QFile::remove(next);
    if (!QFile::link(u"themes/"_s + name, next))
        return false;
    return std::rename(QFile::encodeName(next).constData(),
                       QFile::encodeName(riley + u"/current-theme"_s).constData()) == 0;
}

int distinctColours(const QImage &image)
{
    QSet<QRgb> colours;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < rgb.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x)
            colours.insert(line[x]);
    }
    return int(colours.size());
}

QQuickItem *textItem(QQuickItem *parent, const QString &text)
{
    if (!parent)
        return nullptr;
    if (parent->property("text").toString() == text && parent->property("truncated").isValid())
        return parent;
    const auto children = parent->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = textItem(child, text))
            return found;
    }
    return nullptr;
}

// A home of its own with Documents named in user-dirs.dirs, and a.txt, b.txt in the root.
void makeHome(const UiHarness &ui)
{
    const QDir scratch(ui.scratch());
    // One file system only, so no drive's trash or mark but the fixture's shows.
    writeFile(scratch.filePath(u"mountinfo"_s), "22 1 253:1 / / rw,relatime shared:1 - xfs /dev/mapper/root rw\n");
    qputenv("RILEY_MOUNTINFO", QFile::encodeName(scratch.filePath(u"mountinfo"_s)));
    const QString home = scratch.filePath(u"home"_s);
    QDir().mkpath(home + u"/Documents"_s);
    writeFile(scratch.filePath(u"config/user-dirs.dirs"_s), "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n");
    qputenv("HOME", home.toUtf8());
    ui.makeFile(u"a.txt"_s);
    ui.makeFile(u"b.txt"_s);
}

// The child run of scalesOnHiDPI (RILEY_MEASURE set, with QT_SCALE_FACTOR as the parent chose):
// prints "row=<device pixels> documentsWhole=<0|1> grab=<device pixels wide>/<logical wide>
// warnings=<n>".
int measureChild()
{
    UiHarness ui;
    makeHome(ui);
    if (!ui.start({ui.root()}))
        return 1;
    QQuickItem *list = ui.view(u"list"_s);
    QQuickWindow *w = ui.window();
    if (!QTest::qWaitFor([&] { return list && list->property("count").toInt() == 2; }))
        return 2;
    QQuickItem *row = UiHarness::delegateAt(list, 0);
    QQuickItem *places = ui.view(u"places"_s);
    QQuickItem *documents = nullptr;
    if (!QTest::qWaitFor([&] {
            documents = textItem(places, u"Documents"_s);
            return documents != nullptr;
        }))
        return 3;
    const QImage grab = w->grabWindow();
    std::printf("row=%g documentsWhole=%d grab=%d/%d warnings=%lld\n",
                row ? row->height() * w->devicePixelRatio() : 0.0,
                documents && !documents->property("truncated").toBool()
                    && documents->property("width").toReal() >= documents->property("implicitWidth").toReal(),
                grab.width(), w->width(), static_cast<long long>(ui.qmlWarnings().size()));
    std::fflush(stdout);
    return 0;
}

struct ChildRun
{
    QString out;
    QString err;
    int code = -1;
};

ChildRun runSelf(const QByteArray &scale)
{
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"RILEY_MEASURE"_s, u"1"_s);
    env.insert(u"QT_SCALE_FACTOR"_s, QString::fromLatin1(scale));
    env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), {});
    ChildRun run;
    if (!child.waitForFinished(60000)) {
        child.kill();
        child.waitForFinished();
    }
    run.out = QString::fromLocal8Bit(child.readAllStandardOutput());
    run.err = QString::fromLocal8Bit(child.readAllStandardError());
    run.code = child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
    return run;
}

QString field(const QString &out, const QString &name)
{
    const QRegularExpression pattern(u"\\b"_s + name + u"=(\\S+)"_s);
    return pattern.match(out).captured(1);
}

} // namespace

class LookUiTests : public QObject
{
    Q_OBJECT

private slots:
    void themeFollowsRiley();
    void noColoursOutsideTheTheme();
    void scalesOnHiDPI();
    void startsQuickly();
    void screenshotsForReview();
};

void LookUiTests::themeFollowsRiley()
{
    UiHarness ui;
    const QString riley = ui.scratch() + u"/config/riley"_s;
    QVERIFY(makeTheme(riley, u"ember"_s,
                      "# Ember\n"
                      "mode = \"dark\"\n"
                      "accent = \"#f59e42\"\n"
                      "background = \"#17130f\"\n"
                      "foreground = \"#dccbb4\"\n"
                      "selection = \"#3a2e22\"\n"));
    QVERIFY(switchTheme(riley, u"ember"_s));
    QVERIFY(QFileInfo(riley + u"/current-theme"_s).isSymLink());
    QVERIFY(ui.makeFile(u"a.txt"_s));

    QVERIFY(ui.start({ui.root()}));
    QObject *theme = themeOf(ui);
    QVERIFY(theme);
    QCOMPARE(theme->property("mode").toString(), u"dark"_s);
    QCOMPARE(colourOf(theme, "accent"), u"#f59e42"_s);
    QCOMPARE(colourOf(theme, "background"), u"#17130f"_s);
    QCOMPARE(colourOf(theme, "foreground"), u"#dccbb4"_s);
    QCOMPARE(colourOf(theme, "selection"), u"#3a2e22"_s);
    // And the window is drawn in them.
    QCOMPARE(ui.window()->color().name(), u"#17130f"_s);
    QCOMPARE(colourOf(theme, "text"), u"#dccbb4"_s);

    // Riley switches to another theme: a new link renamed over the old one.
    const QByteArray emberBefore = readFile(riley + u"/themes/ember/colors.toml"_s);
    QVERIFY(makeTheme(riley, u"sea"_s, "mode = \"dark\"\naccent = \"#7fa4c9\"\nbackground = \"#10161c\"\n"));
    QElapsedTimer clock;
    clock.start();
    QVERIFY(switchTheme(riley, u"sea"_s));
    QTRY_COMPARE_WITH_TIMEOUT(colourOf(theme, "accent"), u"#7fa4c9"_s, 2000);
    QVERIFY2(clock.elapsed() < 2000, qPrintable(QString::number(clock.elapsed())));
    QCOMPARE(colourOf(theme, "background"), u"#10161c"_s);
    QCOMPARE(readFile(riley + u"/themes/ember/colors.toml"_s), emberBefore);

    // colors.toml edited in place.
    QVERIFY(writeFile(riley + u"/themes/sea/colors.toml"_s, "mode = \"dark\"\naccent = \"#a3c97f\"\n"));
    QTRY_COMPARE_WITH_TIMEOUT(colourOf(theme, "accent"), u"#a3c97f"_s, 2000);
    // A colour the theme no longer gives comes from the built-in palette.
    QCOMPARE(colourOf(theme, "background"), QString::fromLatin1(builtinDarkBackground));

    // mode "light": the light variant.
    QVERIFY(writeFile(riley + u"/themes/sea/colors.toml"_s, "mode = \"light\"\naccent = \"#a3c97f\"\n"));
    QTRY_COMPARE_WITH_TIMEOUT(theme->property("mode").toString(), u"light"_s, 2000);
    QCOMPARE(colourOf(theme, "background"), QString::fromLatin1(builtinLightBackground));
    QCOMPARE(ui.window()->color().name(), QString::fromLatin1(builtinLightBackground));

    // No theme at all: the built-in palette, and still a whole window.
    QVERIFY(QFile::remove(riley + u"/current-theme"_s));
    QTRY_COMPARE_WITH_TIMEOUT(colourOf(theme, "accent"), QString::fromLatin1(builtinDarkAccent), 2000);
    QCOMPARE(theme->property("mode").toString(), u"dark"_s);
    QCOMPARE(colourOf(theme, "background"), QString::fromLatin1(builtinDarkBackground));
    QTest::qWait(50);
    QVERIFY(distinctColours(ui.window()->grabWindow()) >= 8);

    // A window started with no theme shows the built-in palette from the start.
    ui.stop();
    QVERIFY(ui.start({ui.root()}));
    theme = themeOf(ui);
    QCOMPARE(colourOf(theme, "accent"), QString::fromLatin1(builtinDarkAccent));
    QVERIFY(distinctColours(ui.window()->grabWindow()) >= 8);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void LookUiTests::noColoursOutsideTheTheme()
{
    // The QML files as the program has them (the module's own resources).
    const QRegularExpression hex(u"#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-zA-Z])"_s);
    const QRegularExpression functions(u"\\bQt\\.(?:rgba|hsla)\\b"_s);
    const QRegularExpression strings(u"\"([^\"\\\\\\n]*)\"|'([^'\\\\\\n]*)'"_s);
    QSet<QString> names;
    for (const QString &name : QColor::colorNames())
        names.insert(name.toLower());
    names.insert(u"transparent"_s);

    int files = 0;
    QStringList found;
    QDirIterator it(u":/qt/qml/RileyFiles"_s, {u"*.qml"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        ++files;
        if (QFileInfo(path).fileName() == u"Theme.qml")
            continue;
        const QString text = QString::fromUtf8(readFile(path));
        const QStringList lines = text.split(u'\n');
        for (int n = 0; n < lines.size(); ++n) {
            const QString &line = lines.at(n);
            const QString where = QFileInfo(path).fileName() + u':' + QString::number(n + 1) + u": "_s;
            if (auto m = hex.match(line); m.hasMatch())
                found.append(where + m.captured(0));
            if (auto m = functions.match(line); m.hasMatch())
                found.append(where + m.captured(0));
            for (auto s = strings.globalMatch(line); s.hasNext();) {
                const auto m = s.next();
                const QString value = (m.captured(1) + m.captured(2)).trimmed().toLower();
                if (names.contains(value))
                    found.append(where + value);
            }
        }
    }
    QVERIFY2(files >= 30, qPrintable(QString::number(files)));
    QVERIFY2(found.isEmpty(), qPrintable(found.join(u'\n')));
}

void LookUiTests::scalesOnHiDPI()
{
    const ChildRun one = runSelf("1");
    const ChildRun two = runSelf("2");
    QVERIFY2(one.code == 0, qPrintable(one.out + one.err));
    QVERIFY2(two.code == 0, qPrintable(two.out + two.err));
    for (const ChildRun *run : {&one, &two}) {
        QVERIFY2(!run->err.contains(u"warning"_s, Qt::CaseInsensitive), qPrintable(run->err));
        QCOMPARE(field(run->out, u"warnings"_s), u"0"_s);
        QCOMPARE(field(run->out, u"documentsWhole"_s), u"1"_s);
    }
    const double rowOne = field(one.out, u"row"_s).toDouble();
    const double rowTwo = field(two.out, u"row"_s).toDouble();
    QVERIFY(rowOne > 0);
    QCOMPARE(rowTwo, rowOne * 2);
    // The picture of the window is drawn at the screen's pixels, not stretched afterwards.
    const QStringList grab = field(two.out, u"grab"_s).split(u'/');
    QCOMPARE(grab.size(), 2);
    QCOMPARE(grab.at(0).toInt(), grab.at(1).toInt() * 2);
}

void LookUiTests::startsQuickly()
{
    QTemporaryDir scratch;
    const QString root = scratch.filePath(u"root"_s);
    QVERIFY(QDir().mkpath(root));
    for (int n = 0; n < 1000; ++n)
        QVERIFY(writeFile(root + u"/file"_s + QString::number(n).rightJustified(4, u'0') + u".txt"_s, "x"));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    env.insert(u"XDG_CONFIG_HOME"_s, scratch.filePath(u"config"_s));
    env.insert(u"XDG_DATA_HOME"_s, scratch.filePath(u"data"_s));
    env.insert(u"XDG_CACHE_HOME"_s, scratch.filePath(u"cache"_s));
    env.insert(u"HOME"_s, scratch.path());

    auto launch = [&](qint64 *ms) {
        QProcess program;
        program.setProcessEnvironment(env);
        QElapsedTimer clock;
        clock.start();
        program.start(QStringLiteral(RILEY_FILES_BINARY), {u"--time-start"_s, root});
        QByteArray out;
        while (!out.contains('\n') && program.waitForReadyRead(10000))
            out += program.readAllStandardOutput();
        *ms = clock.elapsed();
        program.waitForFinished(10000);
        return QString::fromLocal8Bit(out + program.readAllStandardOutput()).trimmed();
    };
    // Once to bring the program and its libraries into memory, as any start after the first.
    qint64 ms = 0;
    launch(&ms);
    const QString line = launch(&ms);
    QVERIFY2(line.startsWith(u"first-frame "_s), qPrintable(line));
    QVERIFY2(line.endsWith(u", 1000 files"_s), qPrintable(line));
    QVERIFY2(ms < 800, qPrintable(QString::number(ms) + u" ms: "_s + line));
    qInfo("from launch to the first frame: %lld ms (%s)", static_cast<long long>(ms), qPrintable(line));
}

void LookUiTests::screenshotsForReview()
{
    UiHarness ui;
    const QString shots = qEnvironmentVariableIsSet("RILEY_SHOTS") ? qEnvironmentVariable("RILEY_SHOTS")
                                                                     : ui.scratch() + u"/shots"_s;
    QVERIFY(QDir().mkpath(shots));
    makeHome(ui);
    for (const QString &name : {u"Music"_s, u"Projects"_s, u"Projects/notes"_s, u"Projects/site"_s})
        QVERIFY(QDir().mkpath(ui.root() + u'/' + name));
    for (const QString &name : {u"Projects/plan.md"_s, u"Projects/site/index.html"_s, u"photo.jpg"_s,
                                u"song.ogg"_s, u"clip.mp4"_s, u"report.pdf"_s, u"main.cpp"_s,
                                u"backup.tar.gz"_s, u"notes.txt"_s})
        QVERIFY(ui.makeFile(name, 1200));
    QVERIFY(writeFile(ui.root() + u"/old draft.txt"_s, "draft"));
    const QString riley = ui.scratch() + u"/config/riley"_s;
    QVERIFY(makeTheme(riley, u"light"_s, "mode = \"light\"\n"));

    QStringList written;
    for (const QString &variant : {u"dark"_s, u"light"_s}) {
        if (variant == u"light")
            QVERIFY(switchTheme(riley, u"light"_s));
        else
            QFile::remove(riley + u"/current-theme"_s);
        QVERIFY(ui.start({ui.root()}));
        QObject *theme = themeOf(ui);
        QTRY_COMPARE(theme->property("mode").toString(), variant);
        QTRY_COMPARE(ui.browser().model()->count(), variant == u"dark" ? 12 : 11);
        if (variant == u"dark") {
            QVERIFY(Riley::Trash::put(ui.root() + u"/old draft.txt"_s));
            QTRY_COMPARE(ui.browser().model()->count(), 11);
        }

        auto shoot = [&](const QString &view) {
            QTest::qWait(150);   // past every fade
            const QImage image = ui.window()->grabWindow();
            const QString file = shots + u'/' + variant + u'-' + view + u".png"_s;
            const int colours = distinctColours(image);
            QVERIFY2(colours >= 8, qPrintable(file + u": "_s + QString::number(colours)));
            QVERIFY(image.save(file));
            written.append(file);
        };
        ui.press(Qt::Key_2, Qt::ControlModifier);
        QTRY_VERIFY(ui.view(u"list"_s));
        shoot(u"list"_s);
        ui.press(Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY(ui.view(u"grid"_s));
        shoot(u"grid"_s);
        ui.press(Qt::Key_3, Qt::ControlModifier);
        QTRY_VERIFY(ui.view(u"columns"_s));
        shoot(u"columns"_s);
        ui.press(Qt::Key_2, Qt::ControlModifier);
        QTRY_VERIFY(ui.view(u"list"_s));
        ui.press(Qt::Key_T);
        QTRY_VERIFY(ui.browser().tree());
        ui.press(Qt::Key_Down);
        ui.press(Qt::Key_Right);
        QTRY_COMPARE(ui.browser().model()->count(), 14);
        shoot(u"tree"_s);
        ui.press(Qt::Key_T);
        ui.press(Qt::Key_Comma, Qt::ControlModifier);
        QTRY_VERIFY(ui.browser().prefs()->property("shown").toBool());
        shoot(u"preferences"_s);
        ui.press(Qt::Key_Escape);
        QTRY_VERIFY(!ui.browser().prefs()->property("shown").toBool());
        ui.browser().showTrash();
        QTRY_VERIFY(ui.view(u"trashView"_s));
        QTRY_COMPARE(ui.view(u"trash"_s)->property("count").toInt(), 1);
        shoot(u"trash"_s);
        QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
        ui.stop();
    }
    QCOMPARE(written.size(), 12);
    qInfo("%s", qPrintable(u"pictures in "_s + shots));
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("RILEY_MEASURE"))
        return measureChild();
    LookUiTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}

#include "lookui_tests.moc"
