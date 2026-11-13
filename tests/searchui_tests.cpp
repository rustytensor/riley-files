// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Search in the main window, driven with keys and the mouse through the real window. Expected
// results are the ones written down in the milestone's test list.

#include "browser.h"
#include "filesearch.h"
#include "uiharness.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

class SearchUiTests : public QObject
{
    Q_OBJECT

private slots:
    void uiSearchWithSlash();
    void uiContentSearch();
    void searchStaysResponsive();
};

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// The value of a delegate property for every row of a view.
QStringList shownStrings(QQuickItem *view, const char *property)
{
    QStringList values;
    const int count = view ? view->property("count").toInt() : 0;
    for (int row = 0; row < count; ++row) {
        QQuickItem *delegate = UiHarness::delegateAt(view, row);
        values.append(delegate ? delegate->property(property).toString() : u"?"_s);
    }
    return values;
}

// The text of the child item with this objectName on a row.
QString childText(QQuickItem *delegate, const QString &name)
{
    if (!delegate)
        return {};
    const auto children = delegate->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == name)
            return child->property("text").toString();
    }
    return {};
}

void click(UiHarness &ui, QQuickItem *item)
{
    QVERIFY(item);
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

QQuickItem *focusItem(UiHarness &ui)
{
    return ui.window()->activeFocusItem();
}

} // namespace

void SearchUiTests::uiSearchWithSlash()
{
    UiHarness ui;
    const QDir root(ui.root());
    QVERIFY(writeFile(root.filePath(u"a/report.txt"_s), "one\n"));
    QVERIFY(writeFile(root.filePath(u"a/b/Report-2.md"_s), "two\n"));
    QVERIFY(writeFile(root.filePath(u"c/notes.txt"_s), "three\n"));
    QVERIFY(writeFile(root.filePath(u".hidden/report.txt"_s), "four\n"));
    QVERIFY(QFile::link(u".."_s, root.filePath(u"a/b/loop"_s)));

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a"_s, u"c"_s}));

    // "/" opens the field, which takes the typing; the results take the folder's place.
    ui.press(Qt::Key_Slash);
    QTRY_VERIFY(ui.browser().searchShown());
    QVERIFY(!ui.browser().searchContents());
    QQuickItem *field = ui.item(u"searchField"_s);
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    QCOMPARE(field->property("text").toString(), QString());
    QTRY_VERIFY(ui.view(u"searchResults"_s));
    QVERIFY(!ui.view(u"list"_s));

    ui.type(u"report"_s);
    QCOMPARE(field->property("text").toString(), u"report"_s);
    QQuickItem *results = ui.view(u"searchResults"_s);
    QVERIFY(results);
    QTRY_VERIFY(!ui.browser().search()->isRunning());
    QTRY_COMPARE(UiHarness::shownNames(results), (QStringList{u"Report-2.md"_s, u"report.txt"_s}));
    // Each with its folder.
    QCOMPARE(shownStrings(results, "place"), (QStringList{u"a/b"_s, u"a"_s}));
    QCOMPARE(childText(UiHarness::delegateAt(results, 0), u"resultFolder"_s), u"in a/b"_s);
    QCOMPARE(childText(UiHarness::delegateAt(results, 1), u"resultFolder"_s), u"in a"_s);
    QCOMPARE(UiHarness::currentName(results), u"Report-2.md"_s);
    QCOMPARE(ui.browser().searchStatus(), u"2 found."_s);

    // Return on report.txt: its folder, the cursor on it, the search closed.
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(UiHarness::currentName(results), u"report.txt"_s);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(!ui.browser().searchShown());
    QCOMPARE(ui.browser().folder(), root.filePath(u"a"_s));
    QTRY_VERIFY(ui.view(u"list"_s));
    list = ui.view(u"list"_s);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"b"_s, u"report.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(list), u"report.txt"_s);
    QVERIFY(!ui.view(u"searchResults"_s));
    QTRY_COMPARE(focusItem(ui), ui.item(u"keys"_s));

    // "/" again, then Escape: back to the folder, as it was.
    ui.press(Qt::Key_Slash);
    QTRY_VERIFY(ui.browser().searchShown());
    QTRY_VERIFY(field->hasActiveFocus());
    QCOMPARE(field->property("text").toString(), QString());
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.browser().searchShown());
    QCOMPARE(ui.browser().folder(), root.filePath(u"a"_s));
    QTRY_VERIFY(ui.view(u"list"_s));
    QCOMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"report.txt"_s);
    QTRY_COMPARE(focusItem(ui), ui.item(u"keys"_s));

    // Keys reach the folder again (Down moves its cursor), and no search ran on the UI thread.
    ui.press(Qt::Key_Up);
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"b"_s);
    QVERIFY(!ui.browser().search()->searchThreads().isEmpty());
    QVERIFY(!ui.browser().search()->searchThreads().contains(QThread::currentThreadId()));

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void SearchUiTests::uiContentSearch()
{
    QVERIFY2(!QStandardPaths::findExecutable(u"rg"_s).isEmpty(), "rg is not installed");
    UiHarness ui;
    const QDir root(ui.root());
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "hello World\n"));
    QVERIFY(writeFile(root.filePath(u"b.md"_s), "nothing\n"));
    QVERIFY(writeFile(root.filePath(u"c.bin"_s), QByteArray("\x00\x01hello", 7)));
    QVERIFY(writeFile(root.filePath(u"sub/d.txt"_s), "second\nHELLO again\n"));
    QVERIFY(writeFile(root.filePath(u".h/e.txt"_s), "hello\n"));

    QVERIFY(ui.start({ui.root()}));
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"sub"_s, u"a.txt"_s, u"b.md"_s, u"c.bin"_s}));

    // Ctrl+Shift+F opens the search inside files; its switch says so.
    ui.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(ui.browser().searchShown());
    QVERIFY(ui.browser().searchContents());
    QQuickItem *field = ui.item(u"searchField"_s);
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    QQuickItem *namesButton = ui.item(u"searchModeNames"_s);
    QQuickItem *contentsButton = ui.item(u"searchModeContents"_s);
    QVERIFY(namesButton && contentsButton);
    QVERIFY(contentsButton->property("on").toBool());
    QVERIFY(!namesButton->property("on").toBool());

    ui.type(u"hello"_s);
    QQuickItem *results = ui.view(u"searchResults"_s);
    QVERIFY(results);
    QTRY_VERIFY(!ui.browser().search()->isRunning());
    QTRY_COMPARE(UiHarness::shownNames(results), (QStringList{u"a.txt"_s, u"d.txt"_s}));
    QCOMPARE(shownStrings(results, "line"), (QStringList{u"1"_s, u"2"_s}));
    QCOMPARE(shownStrings(results, "text"), (QStringList{u"hello World"_s, u"HELLO again"_s}));
    QCOMPARE(shownStrings(results, "place"), (QStringList{QString(), u"sub"_s}));
    // The matching lines are shown, the match marked.
    const QString accent = u"<font color=\""_s;
    const QString first = childText(UiHarness::delegateAt(results, 0), u"resultLine"_s);
    const QString second = childText(UiHarness::delegateAt(results, 1), u"resultLine"_s);
    QVERIFY2(first.startsWith(u"<b>"_s + accent) && first.endsWith(u"\">hello</font></b> World"_s), qPrintable(first));
    QVERIFY2(second.startsWith(u"<b>"_s + accent) && second.endsWith(u"\">HELLO</font></b> again"_s),
             qPrintable(second));

    // Ctrl+Shift+F again goes back to names: no file is named "hello".
    ui.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(!ui.browser().searchContents());
    QVERIFY(ui.browser().searchShown());
    QTRY_VERIFY(namesButton->property("on").toBool());
    QCOMPARE(field->property("text").toString(), u"hello"_s);
    QTRY_VERIFY(!ui.browser().search()->isRunning());
    QTRY_COMPARE(results->property("count").toInt(), 0);
    QCOMPARE(ui.browser().searchStatus(), u"Nothing found."_s);

    // The switch goes to the contents again, and the typing stays in the field.
    click(ui, contentsButton);
    QTRY_VERIFY(ui.browser().searchContents());
    QTRY_VERIFY(!ui.browser().search()->isRunning());
    QTRY_COMPARE(UiHarness::shownNames(results), (QStringList{u"a.txt"_s, u"d.txt"_s}));
    QTRY_VERIFY(field->hasActiveFocus());
    // Return on d.txt: its folder, the cursor on it.
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_Return);
    QTRY_VERIFY(!ui.browser().searchShown());
    QCOMPARE(ui.browser().folder(), root.filePath(u"sub"_s));
    QTRY_VERIFY(ui.view(u"list"_s));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"d.txt"_s);

    // A line from a file is never read as markup.
    QCOMPARE(Browser::markMatches(u"<i>a&b hello</i>"_s, {7, 5}, u"#112233"_s),
             u"&lt;i&gt;a&amp;b <b><font color=\"#112233\">hello</font></b>&lt;/i&gt;"_s);
    QCOMPARE(Browser::markMatches(u"x hello"_s, {2, 5}, u"red\"><img src=x>"_s), u"x <b>hello</b>"_s);

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void SearchUiTests::searchStaysResponsive()
{
    QVERIFY2(!QStandardPaths::findExecutable(u"rg"_s).isEmpty(), "rg is not installed");
    UiHarness ui;
    constexpr int files = 5000;
    const QDir root(ui.root());
    for (int i = 0; i < files; ++i) {
        const QString sub = u"d%1"_s.arg(i / 500);
        QVERIFY(writeFile(root.filePath(u"%1/f%2.txt"_s.arg(sub).arg(i)), "say hello " + QByteArray::number(i) + '\n'));
    }

    QVERIFY(ui.start({ui.root()}));
    QTRY_COMPARE(ui.browser().model()->count(), 10);
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

    ui.press(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(ui.browser().searchShown());
    ui.type(u"hello"_s);
    QTRY_VERIFY_WITH_TIMEOUT(ui.browser().search()->count() == Riley::FileSearch::Limit
                                 && !ui.browser().search()->isRunning(),
                             30000);
    // Scroll through the results while they are there.
    QQuickItem *results = ui.view(u"searchResults"_s);
    QVERIFY(results);
    for (int page = 0; page < 10; ++page) {
        ui.press(Qt::Key_PageDown);
        QTest::qWait(30);
    }
    QTest::qWait(200);
    ask.stop();
    QObject::disconnect(counting);
    frames.append(clock.elapsed());

    qint64 gap = 0;
    for (qsizetype i = 1; i < frames.size(); ++i)
        gap = std::max(gap, frames.at(i) - frames.at(i - 1));
    qInfo("%lld frames, longest gap %lld ms", qint64(frames.size()), gap);

    QCOMPARE(ui.browser().searchStatus(), u"Showing the first 2,000 results."_s);
    QVERIFY(ui.browser().searchCursor() > 0);
    QVERIFY2(frames.size() > 20, qPrintable(u"only %1 frames"_s.arg(frames.size())));
    QVERIFY2(gap <= 250, qPrintable(u"%1 ms without a frame"_s.arg(gap)));
    QVERIFY(!ui.browser().search()->searchThreads().isEmpty());
    QVERIFY2(!ui.browser().search()->searchThreads().contains(QThread::currentThreadId()),
             "search work ran on the UI thread");

    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(SearchUiTests)
#include "searchui_tests.moc"
