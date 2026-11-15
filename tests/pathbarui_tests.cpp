// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The path bar in the real window: a click on its empty part types the location, as Ctrl+L
// does; what is typed opens a folder, a file's folder with the cursor on it, or says why it
// cannot; Tab completes folder names, with a list of them under the field. Expected results are
// the ones written down in the milestone's test list.

#include "browser.h"
#include "uiharness.h"

#include <QDir>
#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// HOME for one test, given back after it.
class HomeFor
{
public:
    explicit HomeFor(const QString &home) : m_before(qgetenv("HOME")) { qputenv("HOME", QFile::encodeName(home)); }
    ~HomeFor() { qputenv("HOME", m_before); }
    HomeFor(const HomeFor &) = delete;
    HomeFor &operator=(const HomeFor &) = delete;

private:
    QByteArray m_before;
};

QString fieldText(const UiHarness &ui)
{
    QQuickItem *field = ui.view(u"location"_s);
    return field ? field->property("text").toString() : u"<no field>"_s;
}

// Starts editing and types a whole location over the selected path.
void typeLocation(UiHarness &ui, const QString &text)
{
    if (!ui.browser().locationEditing())
        ui.press(Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"location"_s));
    QTRY_VERIFY(ui.view(u"location"_s)->hasActiveFocus());
    QMetaObject::invokeMethod(ui.view(u"location"_s), "selectAll");
    ui.type(text);
}

} // namespace

class PathBarUiTests : public QObject
{
    Q_OBJECT

private slots:
    void uiPathBarClickEdits();
    void uiPathBarCompletes();
};

void PathBarUiTests::uiPathBarClickEdits()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    const QString home = QDir(ui.scratch()).filePath(u"home"_s);
    QVERIFY(QDir().mkpath(home));
    HomeFor homeFor(home);
    QVERIFY(ui.start({ui.root()}));
    const QString root = ui.root();
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));

    // A click right of the last part of the path bar: the field, holding the folder's path, all selected.
    QQuickItem *crumbs = ui.view(u"breadcrumb"_s);
    QVERIFY(crumbs);
    QQuickItem *last = ui.view(u"crumb"_s + QString::number(ui.browser().crumbs().size() - 1));
    QVERIFY(last);
    const QPointF lastEnd = last->mapToScene(QPointF(last->width(), last->height() / 2));
    const QPointF barEnd = crumbs->mapToScene(QPointF(crumbs->width() - 4, crumbs->height() / 2));
    QVERIFY2(barEnd.x() > lastEnd.x() + 20, "the path bar has no empty part right of the last crumb");
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier, barEnd.toPoint());
    QTRY_VERIFY(ui.view(u"location"_s));
    QVERIFY(ui.browser().locationEditing());
    QQuickItem *field = ui.view(u"location"_s);
    QTRY_VERIFY(field->hasActiveFocus());
    QCOMPARE(field->property("text").toString(), root);
    QCOMPARE(field->property("selectedText").toString(), root);
    QVERIFY(!ui.view(u"breadcrumb"_s));

    // Escape: the path bar again, the folder as it was.
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"location"_s));
    QTRY_VERIFY(ui.view(u"breadcrumb"_s));
    QCOMPARE(ui.browser().folder(), root);

    // A folder.
    typeLocation(ui, root + u"/Zeta"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), QDir(root).filePath(u"Zeta"_s));
    QTRY_VERIFY(!ui.view(u"location"_s));

    // "~" is the home folder.
    typeLocation(ui, u"~"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), home);

    // A file: its folder, with the cursor on it.
    typeLocation(ui, root + u"/e.txt"_s);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), root);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));
    QTRY_COMPARE(UiHarness::currentName(ui.view(u"list"_s)), u"e.txt"_s);

    // Nothing there: still editing, the folder unchanged, the reason under the field.
    typeLocation(ui, root + u"/nope"_s);
    ui.press(Qt::Key_Return);
    QTest::qWait(100);
    QVERIFY(ui.browser().locationEditing());
    QVERIFY(ui.view(u"location"_s));
    QCOMPARE(fieldText(ui), root + u"/nope"_s);
    QCOMPARE(ui.browser().folder(), root);
    QQuickItem *error = ui.view(u"locationError"_s);
    QVERIFY(error);
    QVERIFY(!error->property("text").toString().trimmed().isEmpty());
    QQuickItem *box = ui.view(u"location"_s);
    QVERIFY(error->mapToScene(QPointF(0, 0)).y() >= box->mapToScene(QPointF(0, box->height())).y());

    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"location"_s));
    QVERIFY(!ui.view(u"locationError"_s));
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void PathBarUiTests::uiPathBarCompletes()
{
    UiHarness ui;
    QVERIFY(ui.makeTreeFixture());
    const QString home = QDir(ui.scratch()).filePath(u"home"_s);
    QVERIFY(QDir().mkpath(home + u"/Documents"_s));
    QVERIFY(QDir().mkpath(home + u"/Music"_s));
    HomeFor homeFor(home);
    QVERIFY(ui.start({ui.root()}));
    const QString root = ui.root();
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s, u"e.txt"_s}));

    // One folder starts so: its whole name and a "/".
    typeLocation(ui, root + u"/Al"_s);
    ui.press(Qt::Key_Tab);
    QTRY_COMPARE(fieldText(ui), root + u"/Alpha/"_s);
    QVERIFY(ui.view(u"location"_s)->hasActiveFocus());
    QVERIFY(!ui.view(u"locationSuggestions"_s));

    // Several: the text as it was, and the folders (no files) in the listing's order under the field.
    typeLocation(ui, root + u"/"_s);
    ui.press(Qt::Key_Tab);
    QTRY_VERIFY(ui.view(u"locationSuggestions"_s));
    QCOMPARE(fieldText(ui), root + u"/"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"locationSuggestions"_s)), (QStringList{u"Alpha"_s, u"Zeta"_s}));

    // Down and Return take the first one.
    ui.press(Qt::Key_Down);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(fieldText(ui), root + u"/Alpha/"_s);
    QVERIFY(ui.browser().locationEditing());
    QCOMPARE(ui.browser().folder(), root);
    QTRY_VERIFY(!ui.view(u"locationSuggestions"_s));

    // "~/" becomes the home folder's path.
    typeLocation(ui, u"~/"_s);
    ui.press(Qt::Key_Tab);
    QTRY_COMPARE(fieldText(ui), home + u"/"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"locationSuggestions"_s)),
                 (QStringList{u"Documents"_s, u"Music"_s}));
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"locationSuggestions"_s));
    QVERIFY(ui.browser().locationEditing());
    typeLocation(ui, u"~/Mu"_s);
    ui.press(Qt::Key_Tab);
    QTRY_COMPARE(fieldText(ui), home + u"/Music/"_s);

    // Return opens what the completion made.
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), home + u"/Music"_s);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

QTEST_MAIN(PathBarUiTests)
#include "pathbarui_tests.moc"
