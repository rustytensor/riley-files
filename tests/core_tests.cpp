// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "folderlisting.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

using namespace Riley;

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    void listingSortsLikeAPerson();
};

namespace {

QStringList namesOf(const ListingResult &listing)
{
    QStringList names;
    for (const DirEntry &entry : listing.entries)
        names.append(entry.name);
    return names;
}

bool makeFile(const QDir &dir, const QString &name)
{
    QFile file(dir.filePath(name));
    return file.open(QIODevice::WriteOnly) && file.write("x") == 1;
}

} // namespace

// Folders come first, then names ignoring case, with numbers taken by value; hidden names only
// when asked for.
void CoreTests::listingSortsLikeAPerson()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir dir(tmp.path());
    QVERIFY(dir.mkdir(QStringLiteral("Beta")));
    QVERIFY(dir.mkdir(QStringLiteral("alpha")));
    for (const char *name : {"file10.txt", "File2.txt", "zeta.md", ".hidden"})
        QVERIFY(makeFile(dir, QString::fromLatin1(name)));

    const ListingResult visible = listFolder(tmp.path());
    QVERIFY(visible.ok());
    QCOMPARE(namesOf(visible), (QStringList{QStringLiteral("alpha"), QStringLiteral("Beta"),
                                            QStringLiteral("File2.txt"), QStringLiteral("file10.txt"),
                                            QStringLiteral("zeta.md")}));

    ListingOptions withHidden;
    withHidden.showHidden = true;
    const ListingResult all = listFolder(tmp.path(), withHidden);
    QVERIFY(all.ok());
    QCOMPARE(all.entries.size(), 6);
    QVERIFY(namesOf(all).contains(QStringLiteral(".hidden")));
}

QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
