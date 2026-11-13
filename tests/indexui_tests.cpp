// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The search index with the window: the real indexer service (riley-files-indexer) scanning
// while the window draws on, the Preferences' Index page driven with the mouse and the keys
// with the service applying each change, and the search answered from the index. Expected
// results are the ones written down in the milestone's test list.

#include "browser.h"
#include "filesearch.h"
#include "indexdatabase.h"
#include "indexer.h"
#include "indexerclient.h"
#include "indexsettings.h"
#include "uiharness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <csignal>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool writeScript(const QString &path, const QByteArray &text)
{
    return writeFile(path, text) && QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void collect(QQuickItem *at, const QString &prefix, QList<QQuickItem *> &into)
{
    if (!at || !at->isVisible())
        return;
    if (at->objectName().startsWith(prefix))
        into.append(at);
    const auto children = at->childItems();
    for (QQuickItem *child : children)
        collect(child, prefix, into);
}

// The items whose objectName starts with `prefix`, top to bottom.
QList<QQuickItem *> itemsTopDown(UiHarness &ui, const QString &prefix)
{
    QList<QQuickItem *> items;
    collect(ui.window()->contentItem(), prefix, items);
    std::sort(items.begin(), items.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->mapToScene(QPointF()).y() < b->mapToScene(QPointF()).y();
    });
    return items;
}

QStringList propertyOf(const QList<QQuickItem *> &items, const char *property)
{
    QStringList values;
    for (QQuickItem *item : items)
        values.append(item->property(property).toString());
    return values;
}

void click(UiHarness &ui, QQuickItem *item)
{
    QVERIFY(item);
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    // Clicks apart, so two are never read as a double-click.
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
}

// The labels of the Preferences rows whose option id starts with `prefix`.
QStringList rowLabels(UiHarness &ui, const QString &prefix)
{
    return propertyOf(itemsTopDown(ui, u"pref_"_s + prefix), "label");
}

QStringList indexSetting(UiHarness &ui, const QString &key)
{
    QSettings settings(ui.settingsFile(), QSettings::IniFormat);
    return settings.value(u"index/"_s + key).toStringList();
}

QStringList names(const QString &db, const QString &text)
{
    Riley::IndexDatabase index;
    if (!index.open(db, Riley::IndexDatabase::ReadOnly))
        return {u"no index"_s};
    return index.nameQuery(text);
}

QStringList contents(const QString &db, const QString &text)
{
    Riley::IndexDatabase index;
    if (!index.open(db, Riley::IndexDatabase::ReadOnly))
        return {u"no index"_s};
    return index.contentsQuery(text);
}

bool alive(qint64 pid)
{
    return pid > 0 && ::kill(pid_t(pid), 0) == 0;
}

// The nice value of each thread of a process, from /proc/<pid>/task/<tid>/stat (field 19).
QList<int> threadNice(qint64 pid, QList<qint64> *tids)
{
    QList<int> values;
    const QDir tasks(u"/proc/%1/task"_s.arg(pid));
    for (const QString &tid : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QByteArray stat = readAll(tasks.filePath(tid + u"/stat"_s));
        const qsizetype close = stat.lastIndexOf(')');
        if (close < 0)
            continue;
        // After the name: field 3 (state) onwards, so field 19 is the 17th.
        const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
        if (fields.size() > 16) {
            values.append(fields.at(16).toInt());
            tids->append(tid.toLongLong());
        }
    }
    return values;
}

// The I/O class of a thread (ioprio_get): 3 is the idle class.
int ioClass(qint64 tid)
{
    const long value = ::syscall(SYS_ioprio_get, 1 /* IOPRIO_WHO_PROCESS */, int(tid));
    return value < 0 ? -1 : int(value >> 13);
}

} // namespace

class IndexUiTests : public QObject
{
    Q_OBJECT

private slots:
    void indexStaysOutOfTheWay();
    void uiIndexPanel();
    void uiSearchUsesTheIndex();
};

void IndexUiTests::indexStaysOutOfTheWay()
{
    UiHarness ui;
    const QString home = ui.scratch() + u"/home"_s;
    qputenv("HOME", home.toUtf8());
    qputenv("RILEY_INDEXER_SOCKET", (ui.scratch() + u"/run/indexer.sock"_s).toUtf8());
    qputenv("RILEY_CHANGES_SOCKET", (ui.scratch() + u"/run/changes.sock"_s).toUtf8());
    // 20,000 files in 100 folders.
    for (int folder = 0; folder < 100; ++folder) {
        const QString dir = home + u"/f%1"_s.arg(folder, 3, 10, QLatin1Char('0'));
        QVERIFY(QDir().mkpath(dir));
        for (int i = 0; i < 200; ++i) {
            QFile file(dir + u"/n%1"_s.arg(i, 3, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
    }
    const QString db = Riley::IndexDatabase::defaultFile();
    QCOMPARE(db, ui.scratch() + u"/data/riley-files/index.sqlite"_s);

    QVERIFY(ui.start({home}));
    QTRY_VERIFY(ui.view(u"list"_s));
    // The Index page of the Preferences, which reads how far the index is while it scans.
    ui.press(Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"preferences"_s));
    ui.press(Qt::Key_PageDown, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"pref_indexStatus"_s));

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

    QProcess indexer;
    indexer.setProgram(QStringLiteral(RILEY_INDEXER_BINARY));
    indexer.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    indexer.start();
    QVERIFY(indexer.waitForStarted(5000));
    const qint64 pid = indexer.processId();

    // Nice 10 and the idle I/O class, on every thread, while it scans.
    QTRY_VERIFY(QFileInfo::exists(ui.scratch() + u"/run/indexer.sock"_s));
    QList<qint64> tids;
    const QList<int> nice = threadNice(pid, &tids);
    QVERIFY(!nice.isEmpty());
    for (qsizetype i = 0; i < nice.size(); ++i) {
        QVERIFY2(nice.at(i) == 10, qPrintable(u"thread %1 runs at nice %2"_s.arg(tids.at(i)).arg(nice.at(i))));
        QVERIFY2(ioClass(tids.at(i)) == 3, qPrintable(u"thread %1 has I/O class %2"_s.arg(tids.at(i)).arg(ioClass(tids.at(i)))));
    }

    // The window says when the scan is done: 20,000 files.
    QTRY_VERIFY_WITH_TIMEOUT(ui.browser().indexStatusText().startsWith(u"20,000 files indexed, "_s), 30000);
    QTRY_COMPARE(ui.browser().indexStateText(), u"Up to date"_s);
    QTest::qWait(300);
    ask.stop();
    QObject::disconnect(counting);
    frames.append(clock.elapsed());

    qint64 gap = 0;
    for (qsizetype i = 1; i < frames.size(); ++i)
        gap = std::max(gap, frames.at(i) - frames.at(i - 1));
    qInfo("scan watched for %lld ms, %lld frames, longest gap %lld ms", clock.elapsed(), qint64(frames.size()), gap);
    QVERIFY2(frames.size() > 20, qPrintable(u"only %1 frames"_s.arg(frames.size())));
    QVERIFY2(gap <= 250, qPrintable(u"%1 ms without a frame"_s.arg(gap)));

    {
        Riley::IndexDatabase index;
        QVERIFY(index.open(db, Riley::IndexDatabase::ReadOnly));
        QCOMPARE(index.fileCount(), qint64(20000));
        QCOMPARE(index.entryCount(), qint64(20100)); // the files and their 100 folders
    }
    struct stat st;
    QCOMPARE(::stat(QFile::encodeName(db).constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 07777), 0600);

    indexer.terminate();
    QVERIFY(indexer.waitForFinished(10000));
    QCOMPARE(indexer.exitCode(), 0);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void IndexUiTests::uiIndexPanel()
{
    UiHarness ui;
    const QString scratch = ui.scratch();
    const QString home = scratch + u"/home"_s;
    const QString docs = home + u"/Docs"_s;
    qputenv("HOME", home.toUtf8());
    qputenv("RILEY_INDEXER_SOCKET", (scratch + u"/run/indexer.sock"_s).toUtf8());
    qputenv("RILEY_CHANGES_SOCKET", (scratch + u"/run/changes.sock"_s).toUtf8());
    QVERIFY(writeFile(docs + u"/a.txt"_s, "hello world\n"));
    QVERIFY(writeFile(home + u"/Other/b.txt"_s, "hello again\n"));
    QVERIFY(writeFile(home + u"/notes.log"_s, "x\n"));
    QVERIFY(writeFile(home + u"/keep.txt"_s, "k\n"));
    QVERIFY(writeFile(home + u"/node_modules/m.js"_s, "m\n"));
    const QString db = Riley::IndexDatabase::defaultFile();

    // systemctl, first on PATH: it notes its arguments and starts the real indexer, as systemd's
    // user manager would.
    const QString bin = scratch + u"/bin"_s;
    QVERIFY(writeScript(bin + u"/systemctl"_s,
                        "#!/bin/sh\n"
                        "printf '%s\\n' \"$@\" >> \"" + QFile::encodeName(scratch) + "/systemctl.args\"\n"
                        "\"" RILEY_INDEXER_BINARY "\" </dev/null >/dev/null 2>&1 &\n"
                        "echo $! > \"" + QFile::encodeName(scratch) + "/indexer.pid\"\n"));
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(bin) + ':' + path);
    const auto servicePid = [&scratch] { return readAll(scratch + u"/indexer.pid"_s).trimmed().toLongLong(); };
    const QByteArray startArgs = "--user\nstart\nriley-files-indexer.service\n";

    QVERIFY(ui.start({home}));
    QTRY_VERIFY(ui.view(u"list"_s));
    // A tall window, so every row of the page is drawn at once.
    ui.window()->resize(1100, 1500);
    QTRY_COMPARE(int(ui.window()->height()), 1500);

    // The first run: the program starts the service through systemd's user manager.
    QVERIFY(Browser::startIndexer(ui.settingsFile()));
    QTRY_COMPARE(readAll(scratch + u"/systemctl.args"_s), startArgs);
    QTRY_VERIFY(alive(servicePid()));
    QTRY_VERIFY_WITH_TIMEOUT(Riley::IndexerClient::send(u"ping"_s, nullptr, 0), 10000);

    // Preferences > Index.
    ui.press(Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"preferences"_s));
    click(ui, ui.view(u"prefPage_Index"_s));
    QTRY_COMPARE(ui.browser().prefs()->page(), 1);
    QTRY_COMPARE(propertyOf(itemsTopDown(ui, u"prefSection"_s), "text"),
                 (QStringList{u"Index"_s, u"Folders"_s, u"Left out"_s, u"Contents"_s}));

    // Status: 4 files (node_modules is left out), the size in MB, up to date.
    // The row is made again whenever the page's lists change, so it is looked up each time.
    const auto statusShown = [&ui] {
        QQuickItem *status = ui.view(u"prefInfo_indexStatus"_s);
        return status ? status->property("text").toString() : QString();
    };
    static const QRegularExpression statusLine(u"^\\d+ files? indexed, \\d+\\.\\d MB$"_s);
    QTRY_VERIFY_WITH_TIMEOUT(statusShown().startsWith(u"4 files indexed, "_s), 15000);
    QVERIFY2(statusLine.match(statusShown()).hasMatch(), qPrintable(statusShown()));
    QTRY_COMPARE(ui.browser().indexStateText(), u"Up to date"_s);
    QCOMPARE(names(db, u"notes"_s), QStringList{home + u"/notes.log"_s});

    // The defaults: home, the six patterns, 1 MB, text only.
    QCOMPARE(rowLabels(ui, u"indexFolder:"_s), QStringList{home});
    QCOMPARE(rowLabels(ui, u"indexExclude:"_s),
             (QStringList{u".cache"_s, u"node_modules"_s, u".git"_s, u"build"_s, u"target"_s, u"__pycache__"_s}));
    QVERIFY(ui.view(u"prefChoice_indexLimit_1 MB"_s));
    QVERIFY(ui.view(u"prefChoice_indexLimit_1 MB"_s)->property("chosen").toBool());
    QVERIFY(ui.view(u"pref_indexTextOnly"_s)->property("on").toBool());

    // Add a folder: Docs, typed.
    QQuickItem *addFolder = ui.view(u"prefText_indexAddFolder"_s);
    click(ui, addFolder);
    QTRY_VERIFY(addFolder->hasActiveFocus());
    ui.type(docs);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(rowLabels(ui, u"indexFolder:"_s), (QStringList{home, docs}));
    QCOMPARE(indexSetting(ui, u"folders"_s), (QStringList{home, docs}));

    // Contents on for Docs: saved, and the service puts its files' contents in the index.
    QVERIFY(!ui.view(u"pref_indexFolder:"_s + docs)->property("on").toBool());
    click(ui, ui.view(u"prefSwitch_indexFolder:"_s + docs));
    QTRY_VERIFY(ui.view(u"pref_indexFolder:"_s + docs)->property("on").toBool());
    QCOMPARE(indexSetting(ui, u"contentFolders"_s), QStringList{docs});
    QTRY_COMPARE_WITH_TIMEOUT(contents(db, u"hello"_s), QStringList{docs + u"/a.txt"_s}, 10000);

    // A pattern left out: *.log, typed; notes.log leaves the index.
    QQuickItem *addExclude = ui.view(u"prefText_indexAddExclude"_s);
    click(ui, addExclude);
    QTRY_VERIFY(addExclude->hasActiveFocus());
    ui.type(u"*.log"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(rowLabels(ui, u"indexExclude:"_s).last(), u"*.log"_s);
    QVERIFY(indexSetting(ui, u"excludes"_s).contains(u"*.log"_s));
    QTRY_COMPARE_WITH_TIMEOUT(names(db, u"notes"_s), QStringList(), 10000);

    // node_modules removed from the patterns: its file comes into the index.
    QCOMPARE(names(db, u"m.js"_s), QStringList());
    click(ui, ui.view(u"prefRemove_indexExclude:node_modules"_s));
    QTRY_VERIFY(!rowLabels(ui, u"indexExclude:"_s).contains(u"node_modules"_s));
    QVERIFY(!indexSetting(ui, u"excludes"_s).contains(u"node_modules"_s));
    QTRY_COMPARE_WITH_TIMEOUT(names(db, u"m.js"_s), QStringList{home + u"/node_modules/m.js"_s}, 10000);

    // The size limit: 4 MB.
    click(ui, ui.view(u"prefChoice_indexLimit_4 MB"_s));
    QTRY_COMPARE(QSettings(ui.settingsFile(), QSettings::IniFormat).value(u"index/contentLimit"_s).toString(), u"4194304"_s);

    // Pause and Resume, with the keys: Tab to the button, Space.
    click(ui, ui.view(u"prefButton_indexPause"_s));
    QTRY_COMPARE(ui.browser().indexStateText(), u"Paused"_s);
    {
        Riley::IndexDatabase index;
        QVERIFY(index.open(db, Riley::IndexDatabase::ReadOnly));
        QVERIFY(index.paused());
    }
    click(ui, ui.view(u"prefButton_indexResume"_s));
    QTRY_COMPARE(ui.browser().indexStateText(), u"Up to date"_s);

    // Rebuild: a file the index was never told about (no change helper here) comes in.
    QVERIFY(writeFile(home + u"/new.txt"_s, "n\n"));
    QTest::qWait(500);
    QCOMPARE(names(db, u"new.txt"_s), QStringList());
    QVERIFY(ui.view(u"prefButton_indexRebuild"_s));
    // Reached with the keys too: the cursor on Rebuild, then Space.
    while (ui.browser().prefs()->cursor() != 4)
        ui.press(Qt::Key_Tab);
    ui.press(Qt::Key_Space);
    QTRY_COMPARE_WITH_TIMEOUT(names(db, u"new.txt"_s), QStringList{home + u"/new.txt"_s}, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(statusShown().startsWith(u"5 files indexed, "_s), 10000);

    // Docs removed: back to home only, its contents out of the index.
    click(ui, ui.view(u"prefRemove_indexFolder:"_s + docs));
    QTRY_COMPARE(rowLabels(ui, u"indexFolder:"_s), QStringList{home});
    QCOMPARE(indexSetting(ui, u"folders"_s), QStringList{home});
    QCOMPARE(indexSetting(ui, u"contentFolders"_s), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(contents(db, u"hello"_s), QStringList(), 10000);
    QCOMPARE(names(db, u"a.txt"_s), QStringList{docs + u"/a.txt"_s});

    // Indexing off: kept, and the service ends. On again: started again through systemd.
    const qint64 first = servicePid();
    click(ui, ui.view(u"prefSwitch_indexEnabled"_s));
    QTRY_COMPARE(QSettings(ui.settingsFile(), QSettings::IniFormat).value(u"index/enabled"_s).toString(), u"false"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!alive(first), 10000);
    QCOMPARE(ui.browser().indexStateText(), u"Turned off"_s);
    click(ui, ui.view(u"prefSwitch_indexEnabled"_s));
    QTRY_COMPARE(QSettings(ui.settingsFile(), QSettings::IniFormat).value(u"index/enabled"_s).toString(), u"true"_s);
    QTRY_COMPARE(readAll(scratch + u"/systemctl.args"_s), startArgs + startArgs);
    QTRY_VERIFY(servicePid() != first && alive(servicePid()));
    QTRY_VERIFY_WITH_TIMEOUT(Riley::IndexerClient::send(u"ping"_s, nullptr, 0), 10000);

    ::kill(pid_t(servicePid()), SIGTERM);
    QTRY_VERIFY_WITH_TIMEOUT(!alive(servicePid()), 10000);
    qputenv("PATH", path);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"preferences"_s));
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void IndexUiTests::uiSearchUsesTheIndex()
{
    UiHarness ui;
    const QString scratch = ui.scratch();
    const QString home = scratch + u"/home"_s;
    const QString docs = home + u"/Docs"_s;
    qputenv("HOME", home.toUtf8());
    qputenv("RILEY_INDEXER_SOCKET", (scratch + u"/run/indexer.sock"_s).toUtf8());
    QVERIFY(writeFile(docs + u"/a.txt"_s, "hello world\n"));
    QVERIFY(writeFile(home + u"/Other/b.txt"_s, "hello again\n"));
    const QString db = Riley::IndexDatabase::defaultFile();

    // The index: home scanned with Docs' contents in it, then 200,000 more entries below
    // home/big, 37 of them with "report" in their names in mixed case. These are only in the
    // index, not on disk, so a walk could never find them.
    QStringList reports;
    {
        Riley::IndexDatabase index;
        QVERIFY(index.open(db));
        Riley::IndexSettings settings = Riley::IndexSettings::defaults();
        settings.contentFolders = {docs};
        Riley::Indexer indexer(index, settings);
        indexer.start();
        QCOMPARE(index.state(), u"done"_s);
        QVERIFY(index.begin());
        Riley::IndexEntry big;
        big.path = home + u"/big"_s;
        big.name = u"big"_s;
        big.folder = true;
        index.insertEntry(big, home);
        static const char *const forms[] = {"report", "Report", "REPORT", "rePort"};
        int k = 0;
        for (int d = 0; d < 200; ++d) {
            const QString dir = home + u"/big/d%1"_s.arg(d, 3, 10, QLatin1Char('0'));
            Riley::IndexEntry folder;
            folder.path = dir;
            folder.name = QFileInfo(dir).fileName();
            folder.folder = true;
            index.insertEntry(folder, home + u"/big"_s);
            for (int f = 0; f < 999; ++f, ++k) {
                Riley::IndexEntry file;
                const bool report = k % 5400 == 7 && reports.size() < 37;
                file.name = report ? u"q%1-%2.txt"_s.arg(k).arg(QLatin1String(forms[reports.size() % 4]))
                                   : u"n%1.dat"_s.arg(k, 6, 10, QLatin1Char('0'));
                file.path = dir + u'/' + file.name;
                file.size = 10;
                if (report)
                    reports.append(file.path);
                index.insertEntry(file, dir);
            }
        }
        QVERIFY(index.commit());
        QCOMPARE(reports.size(), 37);
        QVERIFY2(index.entryCount() >= 200000, qPrintable(QString::number(index.entryCount())));
    }
    std::sort(reports.begin(), reports.end());

    // ripgrep, first on PATH, notes how it is run and runs the real one.
    const QString rg = QStandardPaths::findExecutable(u"rg"_s);
    QVERIFY(!rg.isEmpty());
    const QString bin = scratch + u"/bin"_s;
    QVERIFY(writeScript(bin + u"/rg"_s, "#!/bin/sh\n"
                                        "printf '%s\\n' \"$@\" > \"" + QFile::encodeName(scratch) + "/rg.args\"\n"
                                        "exec \"" + QFile::encodeName(rg) + "\" \"$@\"\n"));
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(bin) + ':' + path);

    QVERIFY(ui.start({home}));
    QTRY_VERIFY(ui.view(u"list"_s));
    Riley::FileSearch *search = ui.browser().search();

    // "/" and "repor"; then the keystroke "t" is timed until its first results are in.
    ui.press(Qt::Key_Slash);
    QTRY_VERIFY(ui.browser().searchShown());
    QTRY_VERIFY(ui.item(u"searchField"_s)->hasActiveFocus());
    ui.type(u"repor"_s);
    QTRY_VERIFY(!search->isRunning());
    QTRY_COMPARE(search->count(), 37);
    QElapsedTimer clock;
    qint64 firstRows = -1;
    const QMetaObject::Connection timing = QObject::connect(search, &QAbstractItemModel::rowsInserted, search, [&] {
        if (firstRows < 0)
            firstRows = clock.elapsed();
    });
    clock.start();
    ui.type(u"t"_s);
    QTRY_VERIFY(firstRows >= 0);
    QObject::disconnect(timing);
    qInfo("first results %lld ms after the keystroke", firstRows);
    QVERIFY2(firstRows < 100, qPrintable(u"first results after %1 ms"_s.arg(firstRows)));
    QTRY_VERIFY(!search->isRunning());
    QStringList found;
    for (const Riley::SearchHit &hit : search->hits()) {
        found.append(hit.path);
        QVERIFY2(hit.fromIndex, qPrintable(hit.path));
    }
    std::sort(found.begin(), found.end());
    QCOMPARE(found, reports);
    QTRY_VERIFY(ui.view(u"searchResults"_s));
    QTRY_COMPARE(ui.view(u"searchResults"_s)->property("count").toInt(), 37);
    QCOMPARE(ui.browser().searchStatus(), u"37 found."_s);

    // Inside files: the index for Docs, ripgrep for the rest (told to leave Docs out), merged.
    ui.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(ui.browser().searchContents());
    QQuickItem *field = ui.item(u"searchField"_s);
    QTRY_VERIFY(field->hasActiveFocus());
    field->setProperty("text", QString());
    ui.browser().setSearchText(QString());
    ui.type(u"hello"_s);
    QTRY_VERIFY(!search->isRunning());
    QTRY_COMPARE(search->count(), 2);
    const Riley::SearchHit first = search->hit(0);
    const Riley::SearchHit second = search->hit(1);
    QCOMPARE(first.path, docs + u"/a.txt"_s);
    QCOMPARE(first.line, 1);
    QCOMPARE(first.text, u"hello world"_s);
    QCOMPARE(first.matches, (QList<int>{0, 5}));
    QVERIFY(first.fromIndex);
    QCOMPARE(second.path, home + u"/Other/b.txt"_s);
    QCOMPARE(second.line, 1);
    QCOMPARE(second.text, u"hello again"_s);
    QVERIFY(!second.fromIndex);
    const QStringList rgArgs = QString::fromUtf8(readAll(scratch + u"/rg.args"_s)).split(u'\n');
    const qsizetype glob = rgArgs.indexOf(u"--glob"_s);
    QVERIFY2(glob >= 0 && rgArgs.value(glob + 1) == u"!"_s + docs, qPrintable(rgArgs.join(u' ')));
    QTRY_COMPARE(ui.view(u"searchResults"_s)->property("count").toInt(), 2);

    qputenv("PATH", path);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.browser().searchShown());
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(IndexUiTests)
#include "indexui_tests.moc"
