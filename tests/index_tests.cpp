// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The search index on real files: the first scan with its exclude patterns, name queries on a
// large index, contents only where chosen, settings applied to what is indexed, Pause, Resume
// and Rebuild, and the catch-up after a restart. HOME and the XDG folders point at temporary
// folders.

#include "filesearch.h"
#include "indexdatabase.h"
#include "indexer.h"
#include "indexsettings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Riley;

class IndexTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void indexFirstScan();
    void indexNameQueryIsInstant();
    void indexContentsWhereChosen();
    void indexSettingsApply();
    void indexCatchesUpAfterARestart();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_home;
};

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// An empty file, quickly (the large fixtures hold hundreds of thousands).
bool touch(const QString &path)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

struct DiskRow
{
    QString path;
    bool folder = false;
    qint64 size = 0;
    bool operator==(const DiskRow &) const = default;
};

// What is on disk below root, the way the index should hold it: no links followed, the
// settings' patterns left out with everything below them.
QList<DiskRow> diskRows(const QString &root, const IndexSettings &settings)
{
    QList<DiskRow> rows;
    QStringList folders{root};
    while (!folders.isEmpty()) {
        const QString folder = folders.takeLast();
        const QFileInfoList list = QDir(folder).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                              | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : list) {
            const QString path = folder + u'/' + info.fileName();
            if (settings.isExcluded(info.fileName(), path))
                continue;
            const bool isDir = info.isDir() && !info.isSymLink();
            rows.append({path, isDir, isDir ? 0 : info.size()});
            if (isDir)
                folders.append(path);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DiskRow &a, const DiskRow &b) { return a.path < b.path; });
    return rows;
}

QList<DiskRow> indexRows(IndexDatabase &db)
{
    QList<DiskRow> rows;
    for (const IndexEntry &entry : db.entries())
        rows.append({entry.path, entry.folder, entry.size});
    return rows;
}

IndexSettings settingsFor(const QString &root)
{
    IndexSettings settings = IndexSettings::defaults();
    settings.folders = {root};
    return settings;
}

QStringList firstDifferences(const QList<DiskRow> &a, const QList<DiskRow> &b)
{
    QStringList out;
    QSet<QString> left, right;
    for (const DiskRow &row : a)
        left.insert(row.path + (row.folder ? u"/" : u"") + u' ' + QString::number(row.size));
    for (const DiskRow &row : b)
        right.insert(row.path + (row.folder ? u"/" : u"") + u' ' + QString::number(row.size));
    for (const QString &s : left - right)
        out.append(QStringLiteral("disk only: ") + s);
    for (const QString &s : right - left)
        out.append(QStringLiteral("index only: ") + s);
    out.sort();
    return out.mid(0, 10);
}

} // namespace

void IndexTests::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    m_home = m_tmp->path() + QStringLiteral("/home");
    QVERIFY(QDir().mkpath(m_home));
    qputenv("HOME", QFile::encodeName(m_home));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_tmp->path() + QStringLiteral("/data")));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_tmp->path() + QStringLiteral("/config")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(m_tmp->path() + QStringLiteral("/cache")));
}

// 100 folders holding 1,000 files, plus node_modules/ (50 files) and .cache/ (20 files) -> exactly
// 1,100 entries, none under node_modules or .cache; status "done"; under 10 s.
void IndexTests::indexFirstScan()
{
    for (int d = 0; d < 100; ++d)
        for (int f = 0; f < 10; ++f)
            QVERIFY(writeFile(QStringLiteral("%1/d%2/f%3.txt").arg(m_home).arg(d, 3, 10, u'0').arg(f), "x"));
    for (int f = 0; f < 50; ++f)
        QVERIFY(writeFile(QStringLiteral("%1/node_modules/m%2.js").arg(m_home).arg(f), "m"));
    for (int f = 0; f < 20; ++f)
        QVERIFY(writeFile(QStringLiteral("%1/.cache/c%2").arg(m_home).arg(f), "c"));

    // The default settings: home, with the default exclude patterns.
    const IndexSettings settings = IndexSettings::defaults();
    QCOMPARE(settings.folders, QStringList{QDir::homePath()});
    QCOMPARE(QDir::homePath(), m_home);

    IndexDatabase db;
    QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
    QCOMPARE(db.file(), m_tmp->path() + QStringLiteral("/data/riley-files/index.sqlite"));
    QCOMPARE(db.state(), QStringLiteral("empty"));
    Indexer indexer(db, settings);
    QElapsedTimer timer;
    timer.start();
    indexer.start();
    const qint64 took = timer.elapsed();
    qInfo("first scan of 1,170 files and folders: %lld ms", took);
    QVERIFY2(took < 10000, qPrintable(QString::number(took)));

    QCOMPARE(db.entryCount(), qint64(1100));
    const QList<IndexEntry> entries = db.entries();
    QCOMPARE(int(std::count_if(entries.begin(), entries.end(), [](const IndexEntry &e) { return e.folder; })), 100);
    for (const IndexEntry &entry : entries) {
        QVERIFY2(!entry.path.contains(QLatin1String("/node_modules")), qPrintable(entry.path));
        QVERIFY2(!entry.path.contains(QLatin1String("/.cache")), qPrintable(entry.path));
    }
    const QList<DiskRow> disk = diskRows(m_home, settings);
    QCOMPARE(disk.size(), 1100);
    QVERIFY2(indexRows(db) == disk, qPrintable(firstDifferences(disk, indexRows(db)).join(u'\n')));
    QCOMPARE(db.state(), QStringLiteral("done"));
    QCOMPARE(indexer.state(), QStringLiteral("done"));
    struct stat st;
    QCOMPARE(::stat(QFile::encodeName(db.file()).constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 0777), 0600);
}

// An index of 200,000 entries with 37 names holding "report" in mixed case -> a name query for
// "report" gives exactly those 37 paths in under 50 ms (median of 20 queries).
void IndexTests::indexNameQueryIsInstant()
{
    // 400 folders of 499 files: 200,000 entries.
    const QStringList reportNames = {QStringLiteral("report.txt"),     QStringLiteral("Report.pdf"),
                                     QStringLiteral("REPORT"),         QStringLiteral("monthly-RePoRt.ods"),
                                     QStringLiteral("old_reportage.md"), QStringLiteral("xREPORTx")};
    QStringList expected;
    int made = 0;
    for (int d = 0; d < 400; ++d) {
        const QString folder = QStringLiteral("%1/folder%2").arg(m_home).arg(d, 3, 10, u'0');
        QVERIFY(QDir().mkdir(folder));
        for (int f = 0; f < 499; ++f) {
            QString name = QStringLiteral("file-%1.dat").arg(f, 3, 10, u'0');
            if (f == 7 && made < 37 && d % 10 == 3) {
                name = QString::number(made) + reportNames.at(made % reportNames.size());
                ++made;
            }
            if (f == 9 && made < 37 && d % 10 == 5) {
                name = QString::number(made) + reportNames.at(made % reportNames.size());
                ++made;
            }
            const QString path = folder + u'/' + name;
            QVERIFY(touch(path));
            if (name.contains(QLatin1String("report"), Qt::CaseInsensitive))
                expected.append(path);
        }
    }
    QCOMPARE(expected.size(), 37);
    std::sort(expected.begin(), expected.end());

    IndexDatabase db;
    QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
    Indexer indexer(db, settingsFor(m_home));
    QElapsedTimer scan;
    scan.start();
    indexer.start();
    qInfo("scan of 200,000 entries: %lld ms", scan.elapsed());
    QCOMPARE(db.entryCount(), qint64(200000));

    // As the window asks: its own read-only connection.
    IndexDatabase reader;
    QVERIFY2(reader.open(IndexDatabase::defaultFile(), IndexDatabase::ReadOnly), qPrintable(reader.errorString()));
    QList<qint64> times;
    for (int i = 0; i < 20; ++i) {
        QElapsedTimer timer;
        timer.start();
        const QStringList found = reader.nameQuery(QStringLiteral("report"));
        times.append(timer.nsecsElapsed());
        QCOMPARE(found, expected);
    }
    std::sort(times.begin(), times.end());
    const double medianMs = double(times.at(9) + times.at(10)) / 2 / 1e6;
    qInfo("name query median: %.3f ms", medianMs);
    QVERIFY2(medianMs < 50, qPrintable(QString::number(medianMs)));

    // Mixed case in the query too, and below one folder only.
    QCOMPARE(reader.nameQuery(QStringLiteral("RePort")), expected);
    const QString below = QStringLiteral("%1/folder003").arg(m_home);
    const QStringList inOne = reader.nameQuery(QStringLiteral("report"), below);
    QCOMPARE(inOne.size(), 1);
    QVERIFY(inOne.first().startsWith(below + u'/'));
}

// Contents on for Docs only: Docs/a.txt "hello world" is found by a contents query for "hello";
// Other/b.txt "hello again" is not (ripgrep still finds it); a 2 MB text file in Docs over the
// 1 MB limit and a binary file in Docs are not content-indexed.
void IndexTests::indexContentsWhereChosen()
{
    const QString docs = m_home + QStringLiteral("/Docs");
    const QString other = m_home + QStringLiteral("/Other");
    QVERIFY(writeFile(docs + QStringLiteral("/a.txt"), "hello world\n"));
    QVERIFY(writeFile(other + QStringLiteral("/b.txt"), "hello again\n"));
    QByteArray big;
    while (big.size() < 2 * 1024 * 1024)
        big += "hello there, this line is long text and repeats\n";
    QVERIFY(writeFile(docs + QStringLiteral("/big.txt"), big));
    QVERIFY(writeFile(docs + QStringLiteral("/data.bin"), QByteArray("hello\0\x01\x02\xff\xfe binary", 22)));

    IndexSettings settings = settingsFor(m_home);
    settings.contentFolders = {docs};
    QCOMPARE(settings.contentLimit, qint64(1024 * 1024));
    QVERIFY(settings.textOnly);

    IndexDatabase db;
    QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
    Indexer indexer(db, settings);
    indexer.start();
    QCOMPARE(db.entryCount(), qint64(6));

    IndexDatabase reader;
    QVERIFY(reader.open(IndexDatabase::defaultFile(), IndexDatabase::ReadOnly));
    QCOMPARE(reader.contentsQuery(QStringLiteral("hello")), QStringList{docs + QStringLiteral("/a.txt")});
    QCOMPARE(reader.contentsQuery(QStringLiteral("hello world")), QStringList{docs + QStringLiteral("/a.txt")});
    QCOMPARE(reader.contentsQuery(QStringLiteral("again")), QStringList{});
    QCOMPARE(reader.contentsQuery(QStringLiteral("binary")), QStringList{});
    QCOMPARE(reader.contentsQuery(QStringLiteral("repeats")), QStringList{});
    QCOMPARE(reader.contentCount(), qint64(1));
    // The window knows where the index answers and where ripgrep has to.
    QVERIFY(reader.contentsCover(docs));
    QVERIFY(reader.contentsCover(docs + QStringLiteral("/a.txt")));
    QVERIFY(!reader.contentsCover(other));
    QVERIFY(!reader.contentsCover(m_home));

    // Other is still searched, through ripgrep.
    FileSearch search;
    QSignalSpy finished(&search, &FileSearch::finished);
    search.start(other, QStringLiteral("hello"), FileSearch::Contents, false);
    QVERIFY(finished.wait(10000));
    QCOMPARE(search.count(), 1);
    QCOMPARE(search.hit(0).path, other + QStringLiteral("/b.txt"));

    // A file written later is read when its change is applied.
    QVERIFY(writeFile(docs + QStringLiteral("/c.txt"), "hello later\n"));
    indexer.noteChange(docs + QStringLiteral("/c.txt"));
    indexer.flush();
    QCOMPARE(reader.contentsQuery(QStringLiteral("hello")),
             (QStringList{docs + QStringLiteral("/a.txt"), docs + QStringLiteral("/c.txt")}));
}

// Adding "*.log" removes every .log entry already indexed; pausing holds back changes until
// Resume, which applies them; Rebuild gives exactly the rows a fresh first scan gives.
void IndexTests::indexSettingsApply()
{
    for (int d = 0; d < 5; ++d) {
        for (int f = 0; f < 4; ++f) {
            QVERIFY(writeFile(QStringLiteral("%1/p%2/n%3.txt").arg(m_home).arg(d).arg(f), "n"));
            QVERIFY(writeFile(QStringLiteral("%1/p%2/n%3.log").arg(m_home).arg(d).arg(f), "log"));
        }
    }
    QVERIFY(writeFile(m_home + QStringLiteral("/top.log"), "log"));
    QVERIFY(writeFile(m_home + QStringLiteral("/logs.d/keep.txt"), "k"));

    // Kept in the program's settings file, as the window saves them.
    IndexSettings settings = settingsFor(m_home);
    QVERIFY(settings.saveFile(IndexSettings::defaultFile()));
    QCOMPARE(IndexSettings::loadFile(IndexSettings::defaultFile()), settings);

    IndexDatabase db;
    QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
    Indexer indexer(db, IndexSettings::loadFile(IndexSettings::defaultFile()));
    indexer.start();
    QCOMPARE(db.entryCount(), qint64(5 + 40 + 1 + 2));
    QCOMPARE(db.nameQuery(QStringLiteral(".log")).size(), 21);

    settings.excludes.append(QStringLiteral("*.log"));
    QVERIFY(settings.saveFile(IndexSettings::defaultFile()));
    indexer.applySettings(IndexSettings::loadFile(IndexSettings::defaultFile()));
    QCOMPARE(db.nameQuery(QStringLiteral(".log")), QStringList{});
    QCOMPARE(db.entryCount(), qint64(5 + 20 + 2));
    QCOMPARE(indexRows(db), diskRows(m_home, settings));

    // Taking the pattern out again brings them back.
    settings.excludes.removeLast();
    indexer.applySettings(settings);
    QCOMPARE(db.nameQuery(QStringLiteral(".log")).size(), 21);
    settings.excludes.append(QStringLiteral("*.log"));
    indexer.applySettings(settings);
    QCOMPARE(db.entryCount(), qint64(27));

    // Paused: changes wait.
    indexer.pause();
    QCOMPARE(indexer.state(), QStringLiteral("paused"));
    QVERIFY(writeFile(m_home + QStringLiteral("/p1/new.txt"), "new"));
    QVERIFY(QFile::remove(m_home + QStringLiteral("/p2/n0.txt")));
    indexer.noteChange(m_home + QStringLiteral("/p1/new.txt"));
    indexer.noteChange(m_home + QStringLiteral("/p2/n0.txt"));
    indexer.flush();
    QCOMPARE(db.nameQuery(QStringLiteral("new.txt")), QStringList{});
    QVERIFY(db.children(m_home + QStringLiteral("/p2")).contains(QStringLiteral("n0.txt")));
    QCOMPARE(int(indexer.pendingChanges()), 2);
    // Resume applies them.
    indexer.resume();
    QCOMPARE(indexer.state(), QStringLiteral("done"));
    QCOMPARE(db.nameQuery(QStringLiteral("new.txt")), QStringList{m_home + QStringLiteral("/p1/new.txt")});
    QVERIFY(!db.children(m_home + QStringLiteral("/p2")).contains(QStringLiteral("n0.txt")));
    QCOMPARE(indexRows(db), diskRows(m_home, settings));

    // Rebuild against a fresh first scan into another file.
    QVERIFY(writeFile(m_home + QStringLiteral("/p3/unseen.txt"), "u")); // no change reported
    indexer.rebuild();
    IndexDatabase fresh;
    QVERIFY(fresh.open(m_tmp->path() + QStringLiteral("/fresh.sqlite")));
    Indexer freshIndexer(fresh, settings);
    freshIndexer.start();
    const QList<IndexEntry> rebuilt = db.entries();
    const QList<IndexEntry> scanned = fresh.entries();
    QCOMPARE(rebuilt.size(), scanned.size());
    for (qsizetype i = 0; i < rebuilt.size(); ++i)
        QVERIFY2(rebuilt.at(i).sameRow(scanned.at(i)), qPrintable(rebuilt.at(i).path + QStringLiteral(" / ") + scanned.at(i).path));
    QCOMPARE(db.nameQuery(QStringLiteral("unseen")).size(), 1);
    QCOMPARE(db.state(), QStringLiteral("done"));
}

// The indexer stopped, 5 files made and 2 removed, started again -> the index matches the disk
// within 30 s, with fewer than 10% of the folders read again.
void IndexTests::indexCatchesUpAfterARestart()
{
    for (int d = 0; d < 20; ++d)
        for (int s = 0; s < 10; ++s)
            for (int f = 0; f < 5; ++f)
                QVERIFY(writeFile(QStringLiteral("%1/a%2/b%3/f%4.txt").arg(m_home).arg(d).arg(s).arg(f), "f"));
    const IndexSettings settings = settingsFor(m_home);
    {
        IndexDatabase db;
        QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
        Indexer indexer(db, settings);
        indexer.start();
        QCOMPARE(db.entryCount(), qint64(20 + 200 + 1000));
    }
    const int folders = 1 + 20 + 200;

    // While it is stopped.
    for (int i = 0; i < 5; ++i)
        QVERIFY(writeFile(QStringLiteral("%1/a%2/b%3/made%4.txt").arg(m_home).arg(i * 3).arg(i).arg(i), "m"));
    QVERIFY(QFile::remove(m_home + QStringLiteral("/a1/b1/f1.txt")));
    QVERIFY(QFile::remove(m_home + QStringLiteral("/a19/b9/f4.txt")));

    QElapsedTimer timer;
    timer.start();
    IndexDatabase db;
    QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
    Indexer indexer(db, settings);
    indexer.start();
    const qint64 took = timer.elapsed();
    const Indexer::Stats stats = indexer.lastRun();
    qInfo("catch-up: %lld ms, %lld of %d folders read, %lld checked", took, stats.foldersRead, folders,
          stats.foldersChecked);
    QVERIFY(took < 30000);
    const QList<DiskRow> disk = diskRows(m_home, settings);
    QVERIFY2(indexRows(db) == disk, qPrintable(firstDifferences(disk, indexRows(db)).join(u'\n')));
    QCOMPARE(db.entryCount(), qint64(1220 + 5 - 2));
    QVERIFY2(stats.foldersRead * 10 < folders, qPrintable(QString::number(stats.foldersRead)));
    QCOMPARE(stats.foldersChecked, qint64(folders));
}

QTEST_GUILESS_MAIN(IndexTests)
#include "index_tests.moc"
