// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The folder model as the views use it: real folders on disk, read off the main thread,
// watched, sorted, and opened in place as a tree.

#include "entryorder.h"
#include "foldermodel.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>

using namespace Riley;

class ModelTests : public QObject
{
    Q_OBJECT

private slots:
    void caseOnlyNamesStayStable();
    void modelSortsByChoice();
    void modelListsOffTheMainThread();
    void modelWatchesTheFolder();
    void modelShowsHiddenWhenAsked();
    void modelSaysWhyAFolderCannotBeRead();
    void treeOpensFoldersInPlace();
    void treeKeepsOpenFoldersThroughChanges();
};

namespace {

QStringList names(const FolderModel &model)
{
    QStringList list;
    for (int row = 0; row < model.rowCount(); ++row)
        list.append(model.nameAt(row));
    return list;
}

QList<int> depths(const FolderModel &model)
{
    QList<int> list;
    for (int row = 0; row < model.rowCount(); ++row)
        list.append(model.depthAt(row));
    return list;
}

QStringList lines(const FolderModel &model)
{
    QStringList list;
    for (int row = 0; row < model.rowCount(); ++row)
        list.append(model.data(model.index(row), FolderModel::LinesRole).toString());
    return list;
}

int rowNamed(const FolderModel &model, const QString &name)
{
    return int(names(model).indexOf(name));
}

bool makeFile(const QString &path, qint64 size = 0)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(QByteArray(size, 'x')) == size;
}

bool setModified(const QString &path, const QDateTime &when)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite) && file.setFileTime(when, QFileDevice::FileModificationTime);
}

QStringList list(std::initializer_list<const char *> items)
{
    QStringList out;
    for (const char *item : items)
        out.append(QString::fromUtf8(item));
    return out;
}

// root/Alpha/Beta/c.txt, root/Alpha/d.txt, root/Zeta/z.txt, root/e.txt
bool makeTree(const QString &root)
{
    const QDir dir(root);
    return dir.mkpath(QStringLiteral("Alpha/Beta")) && dir.mkpath(QStringLiteral("Zeta"))
        && makeFile(dir.filePath(QStringLiteral("Alpha/Beta/c.txt")))
        && makeFile(dir.filePath(QStringLiteral("Alpha/d.txt")))
        && makeFile(dir.filePath(QStringLiteral("Zeta/z.txt")))
        && makeFile(dir.filePath(QStringLiteral("e.txt")));
}

} // namespace

// Names that differ only in case keep one order, whatever order the disk gives them in.
void ModelTests::caseOnlyNamesStayStable()
{
    const QStringList expected = list({"a", "B", "b"});

    QStringList given = list({"b", "B", "a"});
    std::sort(given.begin(), given.end());
    do {
        QList<DirEntry> entries;
        for (const QString &name : std::as_const(given))
            entries.append(DirEntry{name, QStringLiteral("/x/") + name, false, 0, {}, false});
        EntryOrder().sort(entries);
        QStringList sorted;
        for (const DirEntry &e : std::as_const(entries))
            sorted.append(e.name);
        QCOMPARE(sorted, expected);
    } while (std::next_permutation(given.begin(), given.end()));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (const QString &name : list({"b", "B", "a"}))
        QVERIFY(makeFile(tmp.filePath(name)));
    FolderModel model;
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), expected);
    for (int round = 0; round < 3; ++round) {
        model.reload();
        QTRY_VERIFY(!model.isLoading());
        QCOMPARE(names(model), expected);
    }
}

void ModelTests::modelSortsByChoice()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir dir(tmp.path());
    const QDateTime now = QDateTime::currentDateTime();
    QVERIFY(dir.mkdir(QStringLiteral("Z")));
    QVERIFY(makeFile(dir.filePath(QStringLiteral("a.txt")), 300));
    QVERIFY(makeFile(dir.filePath(QStringLiteral("b.png")), 100));
    QVERIFY(makeFile(dir.filePath(QStringLiteral("c.txt")), 200));
    QVERIFY(setModified(dir.filePath(QStringLiteral("a.txt")), now.addSecs(-3 * 3600)));
    QVERIFY(setModified(dir.filePath(QStringLiteral("b.png")), now.addSecs(-1 * 3600)));
    QVERIFY(setModified(dir.filePath(QStringLiteral("c.txt")), now.addSecs(-2 * 3600)));

    FolderModel model;
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"Z", "a.txt", "b.png", "c.txt"}));

    model.setSortKey(FolderModel::SortBySize);
    QTRY_COMPARE(names(model), list({"Z", "a.txt", "c.txt", "b.png"}));
    model.setSortKey(FolderModel::SortByModified);
    QTRY_COMPARE(names(model), list({"Z", "b.png", "c.txt", "a.txt"}));
    model.setSortKey(FolderModel::SortByKind);
    QTRY_COMPARE(names(model), list({"Z", "b.png", "a.txt", "c.txt"}));
    model.setSortKey(FolderModel::SortByName);
    QTRY_COMPARE(names(model), list({"Z", "a.txt", "b.png", "c.txt"}));

    // A fresh reading in a chosen order comes out in that order too.
    model.setSortKey(FolderModel::SortBySize);
    model.setPath(QDir::rootPath());
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"Z", "a.txt", "c.txt", "b.png"}));
}

void ModelTests::modelListsOffTheMainThread()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    constexpr int Files = 20000;
    for (int i = 0; i < Files; ++i)
        QVERIFY(makeFile(tmp.filePath(QStringLiteral("file%1.txt").arg(i))));

    // Ticks every 10 ms; the main thread is free while the longest gap between them stays short.
    QElapsedTimer clock;
    qint64 lastTick = 0;
    qint64 longestGap = 0;
    QTimer ticker;
    ticker.setInterval(10);
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        const qint64 t = clock.elapsed();
        longestGap = std::max(longestGap, t - lastTick);
        lastTick = t;
    });

    FolderModel model;
    clock.start();
    model.setPath(tmp.path());
    const qint64 setPathMs = clock.elapsed();
    QVERIFY2(setPathMs < 50, qPrintable(QStringLiteral("setting the path took %1 ms").arg(setPathMs)));

    lastTick = clock.elapsed();
    ticker.start();
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), Files, 10000);
    ticker.stop();
    QVERIFY2(longestGap <= 250, qPrintable(QStringLiteral("the main thread stalled %1 ms").arg(longestGap)));
}

void ModelTests::modelWatchesTheFolder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir dir(tmp.path());
    QVERIFY(makeFile(dir.filePath(QStringLiteral("a.txt"))));
    QVERIFY(makeFile(dir.filePath(QStringLiteral("c.txt"))));

    FolderModel model;
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"a.txt", "c.txt"}));

    QVERIFY(makeFile(dir.filePath(QStringLiteral("b.txt"))));
    QTRY_COMPARE_WITH_TIMEOUT(names(model), list({"a.txt", "b.txt", "c.txt"}), 3000);
    QVERIFY(QFile::remove(dir.filePath(QStringLiteral("a.txt"))));
    QTRY_COMPARE_WITH_TIMEOUT(names(model), list({"b.txt", "c.txt"}), 3000);
    QVERIFY(QFile::rename(dir.filePath(QStringLiteral("c.txt")), dir.filePath(QStringLiteral("0.txt"))));
    QTRY_COMPARE_WITH_TIMEOUT(names(model), list({"0.txt", "b.txt"}), 3000);
}

void ModelTests::modelShowsHiddenWhenAsked()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(makeFile(tmp.filePath(QStringLiteral("x.txt"))));
    QVERIFY(makeFile(tmp.filePath(QStringLiteral(".h"))));

    FolderModel model;
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"x.txt"}));
    model.setShowHidden(true);
    QTRY_COMPARE(model.rowCount(), 2);
    QVERIFY(names(model).contains(QStringLiteral(".h")));
    model.setShowHidden(false);
    QTRY_COMPARE(names(model), list({"x.txt"}));
}

void ModelTests::modelSaysWhyAFolderCannotBeRead()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString locked = tmp.filePath(QStringLiteral("locked"));
    QVERIFY(QDir().mkdir(locked));
    QVERIFY(makeFile(locked + QStringLiteral("/inside.txt")));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::Permissions()));

    FolderModel model;
    model.setPath(locked);
    QTRY_VERIFY(!model.error().isEmpty());
    QTRY_VERIFY(!model.isLoading());
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                              | QFileDevice::ExeOwner));
}

void ModelTests::treeOpensFoldersInPlace()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(makeTree(tmp.path()));

    FolderModel model;
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"Alpha", "Zeta", "e.txt"}));
    QCOMPARE(depths(model), (QList<int>{0, 0, 0}));

    QVERIFY(model.expand(rowNamed(model, QStringLiteral("Alpha"))));
    QTRY_COMPARE(names(model), list({"Alpha", "Beta", "d.txt", "Zeta", "e.txt"}));
    QCOMPARE(depths(model), (QList<int>{0, 1, 1, 0, 0}));
    QCOMPARE(model.parentRow(rowNamed(model, QStringLiteral("d.txt"))), 0);
    QCOMPARE(model.parentRow(rowNamed(model, QStringLiteral("Zeta"))), -1);

    QVERIFY(model.expand(rowNamed(model, QStringLiteral("Beta"))));
    QTRY_COMPARE(names(model), list({"Alpha", "Beta", "c.txt", "d.txt", "Zeta", "e.txt"}));
    QCOMPARE(depths(model), (QList<int>{0, 1, 2, 1, 0, 0}));
    QCOMPARE(lines(model), list({"", "├", "│└", "└", "", ""}));
    QCOMPARE(model.parentRow(rowNamed(model, QStringLiteral("c.txt"))), rowNamed(model, QStringLiteral("Beta")));

    QVERIFY(model.collapse(rowNamed(model, QStringLiteral("Alpha"))));
    QCOMPARE(names(model), list({"Alpha", "Zeta", "e.txt"}));

    QVERIFY(model.expand(rowNamed(model, QStringLiteral("Alpha"))));
    QTRY_COMPARE(names(model), list({"Alpha", "Beta", "c.txt", "d.txt", "Zeta", "e.txt"}));
    QVERIFY(model.isOpenAt(rowNamed(model, QStringLiteral("Beta"))));
}

void ModelTests::treeKeepsOpenFoldersThroughChanges()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(makeTree(tmp.path()));
    const QDir dir(tmp.path());

    FolderModel model;
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"Alpha", "Zeta", "e.txt"}));
    QVERIFY(model.expand(rowNamed(model, QStringLiteral("Alpha"))));
    QTRY_COMPARE(names(model), list({"Alpha", "Beta", "d.txt", "Zeta", "e.txt"}));
    QVERIFY(model.expand(rowNamed(model, QStringLiteral("Beta"))));
    QTRY_COMPARE(names(model), list({"Alpha", "Beta", "c.txt", "d.txt", "Zeta", "e.txt"}));

    QVERIFY(makeFile(dir.filePath(QStringLiteral("Alpha/a2.txt"))));
    QTRY_COMPARE_WITH_TIMEOUT(names(model),
                              list({"Alpha", "Beta", "c.txt", "a2.txt", "d.txt", "Zeta", "e.txt"}), 5000);
    QCOMPARE(depths(model), (QList<int>{0, 1, 2, 1, 1, 0, 0}));

    QVERIFY(QDir(dir.filePath(QStringLiteral("Alpha/Beta"))).removeRecursively());
    QTRY_COMPARE_WITH_TIMEOUT(names(model), list({"Alpha", "a2.txt", "d.txt", "Zeta", "e.txt"}), 5000);
    QCOMPARE(depths(model), (QList<int>{0, 1, 1, 0, 0}));

    model.setPath(dir.filePath(QStringLiteral("Zeta")));
    QTRY_COMPARE(names(model), list({"z.txt"}));
    model.setPath(tmp.path());
    QTRY_COMPARE(names(model), list({"Alpha", "Zeta", "e.txt"}));
    QCOMPARE(depths(model), (QList<int>{0, 0, 0}));
    for (int row = 0; row < model.rowCount(); ++row)
        QVERIFY(!model.isOpenAt(row));
}

QTEST_GUILESS_MAIN(ModelTests)
#include "model_tests.moc"
