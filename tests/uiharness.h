// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

class AppWindow;
class Browser;
namespace Riley {
class Thumbnailer;
}
class QQuickItem;
class QQuickWindow;

// Drives the real main window the way a person does: the program's own QML module and C++,
// started from a command line, shown (offscreen), and given keys. Each test makes one.
//
//     UiHarness ui;
//     QVERIFY(ui.start({ui.root()}));          // as `riley-files <root>`
//     QQuickItem *list = ui.view("list");
//     QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{...}));
//     ui.press(Qt::Key_Down);
//     QCOMPARE(UiHarness::currentName(list), u"e.txt"_s);
//
// Views are found by objectName ("list", "grid", "columns", ...). Any QML warning while the
// window runs is kept, and qmlWarnings() should be empty at the end of a test.
class UiHarness
{
public:
    UiHarness();
    ~UiHarness();

    // The windows are drawn in software unless this is called first, before any harness is made:
    // then they are drawn with OpenGL (the test's own platform must give it; see glplatform.cpp).
    static void drawWithOpenGL();

    UiHarness(const UiHarness &) = delete;
    UiHarness &operator=(const UiHarness &) = delete;

    // A scratch folder for this test, and "<scratch>/root" in it (made empty, for fixtures).
    QString scratch() const;
    QString root() const;
    // The settings file the windows read and write: the program's own place for it, which is in
    // the scratch folder (XDG_CONFIG_HOME is "<scratch>/config").
    QString settingsFile() const;

    // Fills root() with the tree fixture: Alpha/Beta/c.txt, Alpha/d.txt, Zeta/z.txt, e.txt.
    bool makeTreeFixture() const;
    // Makes an empty file (and the folders it needs) below root().
    bool makeFile(const QString &relativePath, qint64 size = 0) const;

    // Starts a window as `riley-files <arguments...>` would, shows it and waits until it is
    // active and has drawn. A window already open is closed first; the settings stay.
    bool start(const QStringList &arguments);
    // Closes the window, as quitting the program does.
    void stop();

    QQuickWindow *window() const;
    Browser &browser() const;
    // What makes the window's thumbnails; they go to "<scratch>/cache/thumbnails". The window's
    // config folder (XDG_CONFIG_HOME: pinned folders, user-dirs.dirs, mimeapps.list) is
    // "<scratch>/config", and its data folder (XDG_DATA_HOME: trash, custom icons) "<scratch>/data".
    Riley::Thumbnailer &thumbnailer() const;

    // The item with this objectName in the window, or null.
    QQuickItem *item(const QString &objectName) const;
    // Like item(), and only when it is visible on screen.
    QQuickItem *view(const QString &objectName) const;

    // Key presses sent to the window.
    void press(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void type(const QString &text);

    // What a ListView or GridView shows: the names on its rows, in order (as the delegates
    // say them). A row whose delegate is not made (scrolled away) reads as "?".
    static QStringList shownNames(QQuickItem *view);
    // The name on the view's current row, or empty when there is none.
    static QString currentName(QQuickItem *view);
    // The delegate on a row of a ListView or GridView, or null when it is not made.
    static QQuickItem *delegateAt(QQuickItem *view, int row);
    // The value of a delegate property for every row, e.g. "indent" or "depth".
    static QList<int> shownInts(QQuickItem *view, const char *property);

    QStringList qmlWarnings() const;

private:
    QTemporaryDir m_scratch;
    std::unique_ptr<AppWindow> m_app;
};
