// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Search on real files: by name below a folder, and inside files through ripgrep. Every search
// runs off the main thread, and a new one drops the one before.

#include "filesearch.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace Riley;

class SearchTests : public QObject
{
    Q_OBJECT

private slots:
    void searchFindsNamesBelow();
    void searchStopsWhenTypingGoesOn();
    void searchInsideFiles();
    void searchInsideFilesSaysWhenRipgrepIsMissing();
    void searchStaysResponsive();
    void searchQueriesAreNeverShellCode();
};

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QStringList relativePaths(const FileSearch &search)
{
    QStringList list;
    for (int row = 0; row < search.count(); ++row)
        list.append(search.hit(row).relativePath);
    return list;
}

// "path:line:text" for each row, the way the expected results are written.
QStringList lineHits(const FileSearch &search)
{
    QStringList list;
    for (int row = 0; row < search.count(); ++row) {
        const SearchHit &hit = search.hit(row);
        list.append(QStringLiteral("%1:%2:%3").arg(hit.relativePath).arg(hit.line).arg(hit.text));
    }
    return list;
}

bool searchedOffTheMainThread(const FileSearch &search)
{
    const QSet<Qt::HANDLE> threads = search.searchThreads();
    return !threads.isEmpty() && !threads.contains(QThread::currentThreadId());
}

} // namespace

void SearchTests::searchFindsNamesBelow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(writeFile(root + "/a/report.txt", "1"));
    QVERIFY(writeFile(root + "/a/b/Report-2.md", "2"));
    QVERIFY(writeFile(root + "/c/notes.txt", "3"));
    QVERIFY(writeFile(root + "/.hidden/report.txt", "4"));
    QVERIFY(QFile::link(QStringLiteral(".."), root + "/a/b/loop")); // a/b/loop -> a: a loop

    FileSearch search;
    search.start(root, QStringLiteral("report"), FileSearch::Names, false);
    QVERIFY(search.isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QCOMPARE(relativePaths(search), QStringList({"a/b/Report-2.md", "a/report.txt"}));
    QCOMPARE(search.hit(1).path, root + "/a/report.txt");
    QCOMPARE(search.data(search.index(1), FileSearch::FolderRole).toString(), root + "/a");
    QCOMPARE(search.data(search.index(0), FileSearch::PlaceRole).toString(), QStringLiteral("a/b"));
    QVERIFY(!search.isCapped());
    QVERIFY(search.message().isEmpty());

    search.start(root, QStringLiteral("report"), FileSearch::Names, true);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QCOMPARE(relativePaths(search), QStringList({".hidden/report.txt", "a/b/Report-2.md", "a/report.txt"}));

    QVERIFY2(searchedOffTheMainThread(search), "search work ran on the main thread");
}

void SearchTests::searchStopsWhenTypingGoesOn()
{
    // 20 folders of 1,000 files: in each, 50 hold "xy", 50 more hold "x", the rest neither.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QStringList expectedXy;
    for (int folder = 0; folder < 20; ++folder) {
        const QString sub = QStringLiteral("f%1").arg(folder, 2, 10, QLatin1Char('0'));
        QVERIFY(QDir(root).mkdir(sub));
        for (int i = 0; i < 1000; ++i) {
            const int n = folder * 1000 + i;
            QString name;
            if (i % 20 == 0)
                name = QStringLiteral("xy%1.dat").arg(n, 5, 10, QLatin1Char('0'));
            else if (i % 20 == 1)
                name = QStringLiteral("xq%1.dat").arg(n, 5, 10, QLatin1Char('0'));
            else
                name = QStringLiteral("n%1.dat").arg(n, 5, 10, QLatin1Char('0'));
            QFile file(root + QLatin1Char('/') + sub + QLatin1Char('/') + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            if (name.startsWith(QLatin1String("xy")))
                expectedXy.append(sub + QLatin1Char('/') + name);
        }
    }
    QCOMPARE(expectedXy.size(), 1000);

    FileSearch search;
    bool typedOn = false;
    bool xStillRunning = false;
    quint64 xyGeneration = 0;
    QStringList shownAfter; // every row shown once "xy" began
    connect(&search, &FileSearch::batchAdded, this, [&](quint64, int) {
        if (typedOn)
            return;
        // The first rows of "x" are in; the person types on.
        typedOn = true;
        xStillRunning = search.isRunning();
        search.start(root, QStringLiteral("xy"), FileSearch::Names, false);
        xyGeneration = search.generation();
    });
    connect(&search, &QAbstractItemModel::rowsInserted, this, [&](const QModelIndex &, int first, int last) {
        if (!xyGeneration)
            return;
        for (int row = first; row <= last; ++row)
            shownAfter.append(search.hit(row).relativePath);
    });

    search.start(root, QStringLiteral("x"), FileSearch::Names, false);
    QTRY_VERIFY_WITH_TIMEOUT(typedOn, 10000);
    QVERIFY2(xStillRunning, "the first search had ended before the second began");
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 20000);
    QCOMPARE(search.generation(), xyGeneration);

    for (const QString &path : std::as_const(shownAfter))
        QVERIFY2(path.contains(QLatin1String("xy")), qPrintable("a result of \"x\" was shown: " + path));
    QCOMPARE(relativePaths(search), expectedXy);
    QCOMPARE(shownAfter, expectedXy);
    QVERIFY(!search.isCapped());
}

void SearchTests::searchInsideFiles()
{
    QVERIFY2(!QStandardPaths::findExecutable(QStringLiteral("rg")).isEmpty(), "rg is not installed");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(writeFile(root + "/a.txt", "hello World\n"));
    QVERIFY(writeFile(root + "/b.md", "nothing\n"));
    QVERIFY(writeFile(root + "/c.bin", QByteArray("\x00\x01hello", 7)));
    QVERIFY(writeFile(root + "/sub/d.txt", "second\nHELLO again\n"));
    QVERIFY(writeFile(root + "/.h/e.txt", "hello\n"));

    FileSearch search;
    search.start(root, QStringLiteral("hello"), FileSearch::Contents, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QVERIFY2(search.message().isEmpty(), qPrintable(search.message()));
    QCOMPARE(lineHits(search), QStringList({"a.txt:1:hello World", "sub/d.txt:2:HELLO again"}));
    QCOMPARE(search.hit(0).matches, QList<int>({0, 5}));
    QCOMPARE(search.hit(1).matches, QList<int>({0, 5}));
    QCOMPARE(search.hit(1).path, root + "/sub/d.txt");

    search.start(root, QStringLiteral("hello"), FileSearch::Contents, true);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QCOMPARE(lineHits(search),
             QStringList({".h/e.txt:1:hello", "a.txt:1:hello World", "sub/d.txt:2:HELLO again"}));

    QVERIFY2(searchedOffTheMainThread(search), "search work ran on the main thread");
}

void SearchTests::searchInsideFilesSaysWhenRipgrepIsMissing()
{
    QTemporaryDir dir;
    QTemporaryDir emptyBin;
    QVERIFY(dir.isValid() && emptyBin.isValid());
    const QString root = dir.path();
    QVERIFY(writeFile(root + "/a.txt", "hello World\n"));
    QVERIFY(writeFile(root + "/sub/d.txt", "second\nHELLO again\n"));

    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(emptyBin.path()));
    const auto restore = qScopeGuard([&oldPath] { qputenv("PATH", oldPath); });

    FileSearch search;
    search.start(root, QStringLiteral("hello"), FileSearch::Contents, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QCOMPARE(search.message(), QStringLiteral("Searching inside files needs ripgrep (rg)."));
    QCOMPARE(search.count(), 0);

    search.start(root, QStringLiteral("txt"), FileSearch::Names, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QVERIFY(search.message().isEmpty());
    QCOMPARE(relativePaths(search), QStringList({"a.txt", "sub/d.txt"}));
}

void SearchTests::searchStaysResponsive()
{
    QVERIFY2(!QStandardPaths::findExecutable(QStringLiteral("rg")).isEmpty(), "rg is not installed");
    // 5,000 files in 50 folders, each with one matching line: more than the 2,000 rows kept.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    for (int folder = 0; folder < 50; ++folder) {
        const QString sub = root + QStringLiteral("/d%1").arg(folder, 2, 10, QLatin1Char('0'));
        QVERIFY(QDir().mkdir(sub));
        for (int i = 0; i < 100; ++i) {
            QFile file(sub + QStringLiteral("/f%1.txt").arg(i, 3, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("first line\nsay hello " + QByteArray::number(folder * 100 + i) + "\n");
        }
    }

    QElapsedTimer clock;
    qint64 last = 0;
    qint64 worstGap = 0;
    QTimer tick;
    tick.setInterval(10);
    connect(&tick, &QTimer::timeout, this, [&] {
        const qint64 now = clock.elapsed();
        worstGap = std::max(worstGap, now - last);
        last = now;
    });

    FileSearch search;
    clock.start();
    tick.start();
    search.start(root, QStringLiteral("hello"), FileSearch::Contents, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 30000);
    tick.stop();

    QVERIFY2(worstGap < 250, qPrintable(QStringLiteral("the main thread stalled for %1 ms").arg(worstGap)));
    QCOMPARE(search.count(), FileSearch::Limit);
    QVERIFY(search.isCapped());
    QCOMPARE(search.note(), QStringLiteral("Showing the first 2,000 results."));
    QVERIFY(search.hit(0).text.startsWith(QLatin1String("say hello ")));
    for (int row = 1; row < search.count(); ++row)
        QVERIFY(search.hit(row - 1).relativePath < search.hit(row).relativePath);
    QVERIFY2(searchedOffTheMainThread(search), "search work ran on the main thread");
}

void SearchTests::searchQueriesAreNeverShellCode()
{
    QVERIFY2(!QStandardPaths::findExecutable(QStringLiteral("rg")).isEmpty(), "rg is not installed");
    // What a person types goes to the search as plain text. Quotes and semicolons in it are only
    // characters to look for, never commands to run.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(writeFile(root + "/q.txt", "it's; fine\n"));
    const QString pwned = root + QStringLiteral("/PWNED");

    FileSearch search;
    search.start(root, QStringLiteral("it's; fine"), FileSearch::Contents, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QVERIFY2(search.message().isEmpty(), qPrintable(search.message()));
    QCOMPARE(lineHits(search), QStringList({"q.txt:1:it's; fine"}));

    const QString attack = QStringLiteral("x'; touch ") + pwned + QStringLiteral("; echo '");
    search.start(root, attack, FileSearch::Contents, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QCOMPARE(search.count(), 0);
    QVERIFY2(!QFileInfo::exists(pwned), "a contents search ran the query as a command");

    search.start(root, attack, FileSearch::Names, false);
    QTRY_VERIFY_WITH_TIMEOUT(!search.isRunning(), 10000);
    QCOMPARE(search.count(), 0);
    QVERIFY2(!QFileInfo::exists(pwned), "a name search ran the query as a command");
}

QTEST_GUILESS_MAIN(SearchTests)
#include "search_tests.moc"
