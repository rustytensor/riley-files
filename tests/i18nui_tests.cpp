// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The window in the person's language and within reach of every person: the menus in German and
// French from the system's language, catalogs that translate every word the sources mark, and
// every control with a name for assistive technology and reachable from the keys.

#include "browser.h"
#include "actions.h"
#include "uiharness.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// One file system only, so the sidebar shows no drive but what the test makes.
void fixMounts(const UiHarness &ui)
{
    const QString mountinfo = QDir(ui.scratch()).filePath(u"mountinfo"_s);
    writeFile(mountinfo, "22 1 253:1 / / rw,relatime shared:1 - xfs /dev/mapper/root rw\n");
    qputenv("RILEY_MOUNTINFO", QFile::encodeName(mountinfo));
}

// The child run of translationsSwitchLanguage (RILEY_CHILD=menus, LANGUAGE as the parent chose):
// prints "menus=<title>|<title>|..." as the window's menu bar shows them.
int menusChild()
{
    UiHarness ui;
    fixMounts(ui);
    if (!ui.start({ui.root()}))
        return 1;
    QQuickItem *bar = ui.view(u"menuBar"_s);
    if (!bar)
        return 2;
    QStringList titles;
    const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (item->objectName() == u"menuTitle")
            titles.append(item->property("title").toString());
        const auto children = item->childItems();
        for (QQuickItem *child : children)
            walk(child);
    };
    walk(bar);
    std::printf("menus=%s\n", titles.join(u'|').toUtf8().constData());
    std::fflush(stdout);
    return 0;
}

QStringList menusIn(const QByteArray &language)
{
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"RILEY_CHILD"_s, u"menus"_s);
    env.insert(u"LANGUAGE"_s, QString::fromLatin1(language));
    env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(), {});
    if (!child.waitForFinished(60000)) {
        child.kill();
        child.waitForFinished();
    }
    const QString out = QString::fromUtf8(child.readAllStandardOutput());
    const QRegularExpression line(u"^menus=(.*)$"_s, QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = line.match(out);
    if (!m.hasMatch())
        return {u"<no menus: exit %1, %2>"_s.arg(child.exitCode()).arg(QString::fromUtf8(child.readAllStandardError()).left(300))};
    return m.captured(1).split(u'|');
}

// --- Accessibility --------------------------------------------------------------------------

// The Accessible attached object of an item, when the QML gives it one.
QObject *accessibleOf(const QQuickItem *item)
{
    const auto children = item->children();
    for (QObject *child : children) {
        if (child->inherits("QQuickAccessibleAttached"))
            return child;
    }
    return nullptr;
}

QString accessibleName(const QQuickItem *item)
{
    const QObject *a = item ? accessibleOf(item) : nullptr;
    return a ? a->property("name").toString() : QString();
}

bool accessibleFlag(const QQuickItem *item, const char *flag)
{
    const QObject *a = item ? accessibleOf(item) : nullptr;
    return a && a->property(flag).toBool();
}

bool shownOnScreen(const QQuickItem *item)
{
    for (const QQuickItem *at = item; at; at = at->parentItem()) {
        if (!at->isVisible() || at->opacity() <= 0.0)
            return false;
    }
    return item->width() > 0 && item->height() > 0;
}

// What a person can click or reach with Tab: a mouse area that takes a button, an item with a
// tap handler, an item Tab stops on, and a field that takes typing. Items the QML says are no
// control (Accessible.ignored: a backdrop that keeps clicks from what is behind it) are left out.
bool isControl(const QQuickItem *item)
{
    if (!item->isEnabled() || accessibleFlag(item, "ignored"))
        return false;
    if (item->inherits("QQuickMouseArea"))
        return item->property("acceptedButtons").toInt() != 0;
    if (item->activeFocusOnTab())
        return true;
    if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
        return !item->property("readOnly").toBool();
    const auto children = item->children();
    for (const QObject *child : children) {
        if (child->inherits("QQuickTapHandler") && child->property("enabled").toBool())
            return true;
    }
    return false;
}

// The item that names a control: the control itself, or the item it is part of (a mouse area
// filling a button, row or tile).
const QQuickItem *namedOwner(const QQuickItem *control)
{
    if (!accessibleName(control).isEmpty())
        return control;
    const QQuickItem *parent = control->parentItem();
    if (parent && !accessibleName(parent).isEmpty())
        return parent;
    return nullptr;
}

QString describe(const QQuickItem *item)
{
    QStringList path;
    for (const QQuickItem *at = item; at && path.size() < 4; at = at->parentItem()) {
        QString part = QString::fromLatin1(at->metaObject()->className());
        if (!at->objectName().isEmpty())
            part += u"#"_s + at->objectName();
        path.append(part);
    }
    return path.join(u" < "_s);
}

void collectControls(QQuickItem *item, QList<QQuickItem *> &into)
{
    if (!item || !item->isVisible() || item->opacity() <= 0.0)
        return;
    if (isControl(item) && shownOnScreen(item))
        into.append(item);
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        collectControls(child, into);
}

// Every control below `scope` that has no name, as "<where>: <item path>".
QStringList unnamed(QQuickItem *scope, const QString &where)
{
    QList<QQuickItem *> controls;
    collectControls(scope, controls);
    QStringList missing;
    for (const QQuickItem *control : std::as_const(controls)) {
        if (!namedOwner(control))
            missing.append(where + u": "_s + describe(control));
    }
    if (controls.isEmpty())
        missing.append(where + u": no controls found at all"_s);
    return missing;
}

bool isAncestor(const QQuickItem *ancestor, const QQuickItem *item)
{
    for (const QQuickItem *at = item; at; at = at->parentItem()) {
        if (at == ancestor)
            return true;
    }
    return false;
}

} // namespace

class LanguageUiTests : public QObject
{
    Q_OBJECT

private slots:
    void translationsSwitchLanguage();
    void translationsAreComplete();
    void accessibleEverywhere();
};

void LanguageUiTests::translationsSwitchLanguage()
{
    QCOMPARE(menusIn("de"), (QStringList{u"Datei"_s, u"Bearbeiten"_s, u"Ansicht"_s, u"Gehe zu"_s, u"Hilfe"_s}));
    QCOMPARE(menusIn("fr"), (QStringList{u"Fichier"_s, u"Édition"_s, u"Affichage"_s, u"Aller"_s, u"Aide"_s}));
    QCOMPARE(menusIn("en"), (QStringList{u"File"_s, u"Edit"_s, u"View"_s, u"Go"_s, u"Help"_s}));
}

void LanguageUiTests::translationsAreComplete()
{
    // The catalogs as they are, brought up to date with the sources by Qt's own lupdate in a
    // scratch folder: a string the sources mark that a catalog lacks comes in unfinished, one the
    // sources no longer have stays as vanished.
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString source = QStringLiteral(RILEY_SOURCE_DIR);
    QStringList catalogs;
    for (const char *language : {"de", "fr"}) {
        const QString name = u"riley-files_%1.ts"_s.arg(QLatin1String(language));
        const QString copy = scratch.filePath(name);
        QVERIFY2(QFile::copy(source + u"/i18n/"_s + name, copy), qPrintable(name));
        QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner);
        catalogs.append(copy);
    }
    QProcess lupdate;
    lupdate.setProcessChannelMode(QProcess::MergedChannels);
    lupdate.start(QStringLiteral(RILEY_LUPDATE),
                  QStringList{u"-locations"_s, u"none"_s, u"-extensions"_s, u"cpp,h,qml"_s, source + u"/src"_s,
                              u"-ts"_s} + catalogs);
    QVERIFY(lupdate.waitForFinished(120000));
    const QString said = QString::fromLocal8Bit(lupdate.readAll());
    QVERIFY2(lupdate.exitStatus() == QProcess::NormalExit && lupdate.exitCode() == 0, qPrintable(said));

    static const QRegularExpression placeholder(u"%(\\d+|n)"_s);
    for (const QString &catalog : std::as_const(catalogs)) {
        QFile file(catalog);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QStringList problems;
        QString language;
        QString context;
        QString sourceText;
        QStringList forms;
        QString translation;
        QString type;
        bool numerus = false;
        int messages = 0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto name = xml.name();
                if (name == u"TS") {
                    language = xml.attributes().value(u"language").toString();
                } else if (name == u"name") {
                    context = xml.readElementText();
                } else if (name == u"message") {
                    numerus = xml.attributes().value(u"numerus") == u"yes";
                    sourceText.clear();
                    forms.clear();
                    translation.clear();
                    type.clear();
                } else if (name == u"source") {
                    sourceText = xml.readElementText();
                } else if (name == u"translation") {
                    type = xml.attributes().value(u"type").toString();
                    if (!numerus)
                        translation = xml.readElementText();
                } else if (name == u"numerusform") {
                    forms.append(xml.readElementText());
                }
            } else if (xml.isEndElement() && xml.name() == u"message") {
                ++messages;
                const QString where = context + u": “"_s + sourceText + u"”"_s;
                if (!type.isEmpty())
                    problems.append(where + u" is "_s + type);
                const QStringList texts = numerus ? forms : QStringList{translation};
                if (texts.isEmpty())
                    problems.append(where + u" has no translation"_s);
                for (const QString &text : texts) {
                    if (text.trimmed().isEmpty()) {
                        problems.append(where + u" has an empty translation"_s);
                        continue;
                    }
                    auto it = placeholder.globalMatch(sourceText);
                    while (it.hasNext()) {
                        const QString mark = it.next().captured(0);
                        if (!text.contains(mark))
                            problems.append(where + u" lacks "_s + mark + u" in “"_s + text + u"”"_s);
                    }
                }
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY2(language == u"de" || language == u"fr", qPrintable(catalog + u": language "_s + language));
        // The sources mark some hundreds of strings; an empty catalog proves nothing.
        QVERIFY2(messages >= 300, qPrintable(u"%1: only %2 messages"_s.arg(catalog).arg(messages)));
        QVERIFY2(problems.isEmpty(),
                 qPrintable(u"%1: %2 problems, first: %3"_s.arg(QFileInfo(catalog).fileName())
                                .arg(problems.size())
                                .arg(problems.mid(0, 5).join(u"; "_s))));
    }
}

void LanguageUiTests::accessibleEverywhere()
{
    UiHarness ui;
    fixMounts(ui);
    const QDir root(ui.root());
    QVERIFY(writeFile(root.filePath(u"Alpha/a.txt"_s), "old"));
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "new"));
    QVERIFY(writeFile(root.filePath(u"b.txt"_s), "b"));
    QVERIFY(ui.start({ui.root()}));
    QQuickWindow *window = ui.window();
    QQuickItem *content = window->contentItem();
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"Alpha"_s, u"a.txt"_s, u"b.txt"_s}));

    QStringList missing;
    // The main window in each view, with the details pane, the tree, a message, the search field,
    // the location field and an open menu.
    ui.press(Qt::Key_I, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"details"_s));
    ui.press(Qt::Key_T);
    QTRY_VERIFY(ui.browser().tree());
    QVERIFY(ui.browser().actions()->trigger(u"about"_s)); // a message in the bar
    QTRY_VERIFY(ui.view(u"message"_s));
    missing += unnamed(content, u"list"_s);
    ui.press(Qt::Key_T);
    QTRY_VERIFY(!ui.browser().tree());
    ui.press(Qt::Key_1, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"grid"_s));
    QTRY_VERIFY(UiHarness::delegateAt(ui.view(u"grid"_s), 0));
    missing += unnamed(content, u"grid"_s);
    ui.press(Qt::Key_3, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"column1"_s));
    QTRY_VERIFY(UiHarness::delegateAt(ui.view(u"column1"_s), 0));
    missing += unnamed(content, u"columns"_s);
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"list"_s));
    ui.press(Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"searchField"_s));
    missing += unnamed(content, u"search"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"searchField"_s));
    ui.press(Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"location"_s));
    missing += unnamed(content, u"location"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"location"_s));
    ui.press(Qt::Key_F10);
    QTRY_VERIFY(ui.view(u"menuLevel0"_s));
    missing += unnamed(content, u"menu"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"menuLevel0"_s));

    // Connect to Server.
    QVERIFY(ui.browser().actions()->trigger(u"connect"_s));
    QTRY_VERIFY(ui.view(u"connectField"_s));
    missing += unnamed(ui.view(u"connect"_s), u"Connect to Server"_s);
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"connect"_s));

    // The clash question: a.txt copied into Alpha, which holds an a.txt.
    QTRY_COMPARE(UiHarness::currentName(list), u"Alpha"_s);
    ui.press(Qt::Key_Down);
    QTRY_COMPARE(UiHarness::currentName(list), u"a.txt"_s);
    ui.press(Qt::Key_C, Qt::ControlModifier);
    ui.press(Qt::Key_Up);
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), root.filePath(u"Alpha"_s));
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s}));
    ui.press(Qt::Key_V, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"clashDialog"_s));
    missing += unnamed(ui.view(u"clashDialog"_s), u"clash"_s);
    ui.press(Qt::Key_S); // skip
    QTRY_VERIFY(!ui.view(u"clashDialog"_s));

    // The Preferences: every control named, and Tab going round all of them.
    ui.press(Qt::Key_Comma, Qt::ControlModifier);
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = ui.view(u"preferences"_s)));
    QTRY_VERIFY(UiHarness::delegateAt(ui.view(u"preferenceRows"_s), 0));
    missing += unnamed(panel, u"Preferences"_s);
    QVERIFY2(missing.isEmpty(), qPrintable(u"%1 controls without a name: %2"_s.arg(missing.size())
                                               .arg(missing.mid(0, 8).join(u"; "_s))));

    QList<QQuickItem *> controls;
    collectControls(panel, controls);
    const int rows = ui.view(u"preferenceRows"_s)->property("count").toInt();
    QVERIFY(rows > 0);
    QSet<QQuickItem *> focused;
    for (int i = 0; i < rows + 2; ++i) {
        ui.press(Qt::Key_Tab);
        QTest::qWait(10);
        if (QQuickItem *f = window->activeFocusItem())
            focused.insert(f);
    }
    QStringList unreached;
    for (QQuickItem *control : std::as_const(controls)) {
        bool reached = false;
        for (QQuickItem *f : std::as_const(focused)) {
            // Tab lands on the control itself, or on the focusable control it is part of (a
            // choice's segments are reached with their row, then chosen with Left and Right).
            if (f == control
                || (f != panel && isAncestor(panel, f) && isAncestor(f, control)
                    && (accessibleFlag(f, "focusable") || f->activeFocusOnTab()))) {
                reached = true;
                break;
            }
        }
        if (!reached)
            unreached.append(describe(control));
    }
    QVERIFY2(unreached.isEmpty(), qPrintable(u"Tab does not reach %1 of %2 controls: %3"_s.arg(unreached.size())
                                                 .arg(controls.size())
                                                 .arg(unreached.mid(0, 6).join(u"; "_s))));
    QVERIFY(ui.qmlWarnings().isEmpty());
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (qgetenv("RILEY_CHILD") == "menus")
        return menusChild();
    LanguageUiTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}

#include "i18nui_tests.moc"
