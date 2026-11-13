// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "pinnedfolders.h"
#include "places.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

using namespace Riley;

class SidebarTests : public QObject
{
    Q_OBJECT

private slots:
    void sidebarListsPlaces();
    void pinnedFoldersShareGtkBookmarks();
};

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<unreadable>");
}

QStringList namesOf(const PlacesModel &model)
{
    QStringList names;
    for (const PlacesModel::Place &place : model.places())
        names.append(place.name);
    return names;
}

QStringList namesOf(const PinnedModel &model)
{
    QStringList names;
    for (const PinnedModel::Pin &pin : model.pins())
        names.append(pin.name);
    return names;
}

} // namespace

// HOME and XDG_CONFIG_HOME in a temporary folder; user-dirs.dirs names Documents and Pictures
// (both made) and Music (not made) -> [Home, Documents, Pictures, Trash].
void SidebarTests::sidebarListsPlaces()
{
    QTemporaryDir tmp;
    const QString home = tmp.filePath(QStringLiteral("home"));
    const QString config = tmp.filePath(QStringLiteral("config"));
    QVERIFY(QDir().mkpath(home + QStringLiteral("/Documents")));
    QVERIFY(QDir().mkpath(home + QStringLiteral("/Pictures")));
    QVERIFY(QDir().mkpath(config));
    QVERIFY(writeFile(config + QStringLiteral("/user-dirs.dirs"),
                      "# written by xdg-user-dirs-update\n"
                      "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"
                      "XDG_MUSIC_DIR=\"$HOME/Music\"\n"
                      "XDG_PICTURES_DIR=\"" + QFile::encodeName(home) + "/Pictures\"\n"));
    qputenv("HOME", QFile::encodeName(home));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(config));
    qputenv("XDG_DATA_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("data"))));

    PlacesModel places;
    QCOMPARE(namesOf(places), (QStringList{QStringLiteral("Home"), QStringLiteral("Documents"),
                                           QStringLiteral("Pictures"), QStringLiteral("Trash")}));
    QCOMPARE(places.pathAt(0), home);
    QCOMPARE(places.pathAt(1), home + QStringLiteral("/Documents"));
    QCOMPARE(places.pathAt(2), home + QStringLiteral("/Pictures"));
    QCOMPARE(places.rowOf(home + QStringLiteral("/Pictures/")), 2);
    QCOMPARE(places.rowOf(home + QStringLiteral("/Music")), -1);
}

// bookmarks holds a local line with a label and an sftp line -> Pinned shows [X Label]; pinning
// <tmp>/Y adds "file:///<tmp>/Y" at the end; unpinning removes only that line; the sftp line is
// still there, byte for byte.
void SidebarTests::pinnedFoldersShareGtkBookmarks()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    const QString config = root + QStringLiteral("/config");
    QVERIFY(QDir().mkpath(config + QStringLiteral("/gtk-3.0")));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/x")));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/Y")));
    const QString bookmarks = config + QStringLiteral("/gtk-3.0/bookmarks");
    const QByteArray localLine = "file://" + QFile::encodeName(root) + "/x X Label";
    const QByteArray original = localLine + "\nsftp://host/y\n";
    QVERIFY(writeFile(bookmarks, original));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(config));

    PinnedModel pinned;
    QCOMPARE(pinned.filePath(), bookmarks);
    QCOMPARE(namesOf(pinned), QStringList{QStringLiteral("X Label")});
    QCOMPARE(pinned.pathAt(0), root + QStringLiteral("/x"));

    const QString y = root + QStringLiteral("/Y");
    QVERIFY(pinned.pin(y));
    const QByteArray yLine = "file://" + QFile::encodeName(root) + "/Y";
    QCOMPARE(readFile(bookmarks), original + yLine + "\n");
    QCOMPARE(namesOf(pinned), (QStringList{QStringLiteral("X Label"), QStringLiteral("Y")}));
    QVERIFY(pinned.isPinned(y));

    QVERIFY(pinned.unpin(y));
    QCOMPARE(readFile(bookmarks), original);
    QCOMPARE(namesOf(pinned), QStringList{QStringLiteral("X Label")});
    QVERIFY(!pinned.isPinned(y));

    // Ctrl+D's toggle: on, then off again.
    QVERIFY(pinned.toggle(y));
    QVERIFY(!pinned.toggle(y));
    QCOMPARE(readFile(bookmarks), original);
}

QTEST_GUILESS_MAIN(SidebarTests)
#include "sidebar_tests.moc"
