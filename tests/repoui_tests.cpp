// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The list view as a repository page, in the window: the outlined table with its summing-up band
// and how long ago each item changed, the folder's README rendered under it (and made safe), and
// the folder's path in the header as parts to click.

#include "browser.h"
#include "foldermodel.h"
#include "gitlog.h"
#include "uiharness.h"

#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <fcntl.h>
#include <sys/stat.h>

using namespace Qt::StringLiterals;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// Sets the modified time of a file or folder to `secondsAgo` before now.
bool age(const QString &path, qint64 secondsAgo)
{
    const qint64 when = QDateTime::currentSecsSinceEpoch() - secondsAgo;
    const struct timespec times[2] = {{when, 0}, {when, 0}};
    return ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0) == 0;
}

QObject *themeOf(const UiHarness &ui)
{
    QQmlEngine *engine = qmlEngine(ui.window());
    return engine ? engine->singletonInstance<QObject *>("RileyFiles", "Theme") : nullptr;
}

// The first item below `parent` (itself included) with this objectName.
QQuickItem *child(QQuickItem *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    const auto children = parent->childItems();
    for (QQuickItem *item : children) {
        if (QQuickItem *found = child(item, name))
            return found;
    }
    return nullptr;
}

// The texts of the visible Text items below `parent`, in order.
QStringList shownTexts(QQuickItem *parent)
{
    QStringList texts;
    if (!parent || !parent->isVisible())
        return texts;
    if (parent->property("truncated").isValid() && parent->property("text").isValid())
        texts.append(parent->property("text").toString());
    const auto children = parent->childItems();
    for (QQuickItem *item : children)
        texts += shownTexts(item);
    return texts;
}

void click(UiHarness &ui, QQuickItem *item, QPointF at)
{
    QTest::mouseClick(ui.window(), Qt::LeftButton, Qt::NoModifier, item->mapToScene(at).toPoint());
}

void clickMiddle(UiHarness &ui, QQuickItem *item)
{
    click(ui, item, QPointF(item->width() / 2, item->height() / 2));
}

QTextDocument *documentOf(QQuickItem *textEdit)
{
    auto *quick = textEdit ? textEdit->property("textDocument").value<QQuickTextDocument *>() : nullptr;
    return quick ? quick->textDocument() : nullptr;
}

// The fragment of the document whose text is exactly `text`, or an invalid one.
QTextFragment fragmentOf(QTextDocument *document, const QString &text)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().text() == text)
                return it.fragment();
        }
    }
    return {};
}

// Every link in the document, and every image's source.
QStringList linksOf(QTextDocument *document)
{
    QStringList links;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isAnchor() && !format.anchorHref().isEmpty())
                links.append(format.anchorHref());
        }
    }
    return links;
}

QStringList imagesOf(QTextDocument *document)
{
    QStringList images;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isImageFormat())
                images.append(format.toImageFormat().name());
        }
    }
    return images;
}

QRectF rectangleAt(QQuickItem *textEdit, int position)
{
    QRectF rect;
    QMetaObject::invokeMethod(textEdit, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, position));
    return rect;
}

// Where `word` (a whole word) is in the document: a cursor selecting it, null when it is not.
QTextCursor wordIn(QTextDocument *document, const QString &word)
{
    return document->find(word, 0, QTextDocument::FindWholeWords | QTextDocument::FindCaseSensitively);
}

// Whether the characters of `word` are a link.
bool isLink(QTextDocument *document, const QString &word)
{
    const QTextCursor found = wordIn(document, word);
    return !found.isNull() && found.charFormat().isAnchor() && !found.charFormat().anchorHref().isEmpty();
}

// Where in the README's text item the middle of `word` is drawn, or (-1, -1).
QPointF pointOf(QQuickItem *textEdit, const QString &word)
{
    QTextDocument *document = documentOf(textEdit);
    const QTextCursor found = document ? wordIn(document, word) : QTextCursor();
    if (found.isNull())
        return {-1, -1};
    const QRectF start = rectangleAt(textEdit, found.selectionStart());
    const QRectF end = rectangleAt(textEdit, found.selectionEnd());
    return QPointF((start.x() + end.x()) / 2, start.center().y());
}

qreal lineHeightAt(QQuickItem *textEdit, int position)
{
    return rectangleAt(textEdit, position).height();
}

// Runs git in `folder` as the test's own author, with nothing from the machine's git settings;
// `authorSecondsAgo` sets the author date. Gives what git wrote, or a null array when it failed.
QByteArray git(const QString &folder, const QStringList &arguments, qint64 authorSecondsAgo = 0,
               const QByteArray &input = QByteArray())
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"GIT_CONFIG_NOSYSTEM"_s, u"1"_s);
    env.insert(u"GIT_CONFIG_GLOBAL"_s, u"/dev/null"_s);
    env.insert(u"GIT_AUTHOR_NAME"_s, u"Ada Tester"_s);
    env.insert(u"GIT_AUTHOR_EMAIL"_s, u"ada@example.org"_s);
    env.insert(u"GIT_COMMITTER_NAME"_s, u"Ada Tester"_s);
    env.insert(u"GIT_COMMITTER_EMAIL"_s, u"ada@example.org"_s);
    const qint64 when = QDateTime::currentSecsSinceEpoch() - authorSecondsAgo;
    env.insert(u"GIT_AUTHOR_DATE"_s, u"@%1 +0000"_s.arg(when));
    QProcess process;
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(folder);
    process.start(u"git"_s, arguments);
    if (!process.waitForStarted(5000))
        return {};
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning() << "git" << arguments << process.readAllStandardError();
        return {};
    }
    QByteArray out = process.readAllStandardOutput();
    return out.isNull() ? QByteArray("") : out;
}

// The delegate of the row named `name`, or null.
QQuickItem *rowNamed(QQuickItem *list, const QString &name)
{
    const int count = list->property("count").toInt();
    for (int row = 0; row < count; ++row) {
        QQuickItem *delegate = UiHarness::delegateAt(list, row);
        if (delegate && delegate->property("name").toString() == name)
            return delegate;
    }
    return nullptr;
}

QString textOf(QQuickItem *item)
{
    return item ? item->property("text").toString() : QString();
}

// What the desktop was asked to open.
class UrlCatcher : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl &url) { urls.append(url); }
};

} // namespace

class RepoUiTests : public QObject
{
    Q_OBJECT

private slots:
    void repoTableShowsTheFolder();
    void readmeRenderedBelow();
    void readmeIsSafe();
    void breadcrumbPath();
    void gitFolderShowsLastCommits();
    void gitInfoStaysOffTheUiThread();
};

void RepoUiTests::repoTableShowsTheFolder()
{
    // How long ago, at each step of the scale.
    const QDateTime now = QDateTime::currentDateTime();
    using Riley::FolderModel;
    QCOMPARE(FolderModel::ageText(now, now), u"now"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(-59), now), u"now"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(30), now), u"now"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(-60), now), u"1 minute ago"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(-5 * 60), now), u"5 minutes ago"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(-3600), now), u"1 hour ago"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(-23 * 3600), now), u"23 hours ago"_s);
    QCOMPARE(FolderModel::ageText(now.addSecs(-25 * 3600), now), u"yesterday"_s);
    QCOMPARE(FolderModel::ageText(now.addDays(-2), now), u"2 days ago"_s);
    QCOMPARE(FolderModel::ageText(now.addDays(-6), now), u"6 days ago"_s);
    QCOMPARE(FolderModel::ageText(now.addDays(-7), now), u"1 week ago"_s);
    QCOMPARE(FolderModel::ageText(now.addDays(-21), now), u"3 weeks ago"_s);
    QCOMPARE(FolderModel::ageText(now.addDays(-29), now), u"4 weeks ago"_s);
    const QDateTime old = now.addDays(-40);
    QCOMPARE(FolderModel::ageText(old, now), QLocale().toString(old.date(), QLocale::ShortFormat));
    QCOMPARE(FolderModel::summaryText(2, 2), u"2 folders, 2 files"_s);
    QCOMPARE(FolderModel::summaryText(1, 0), u"1 folder"_s);
    QCOMPARE(FolderModel::summaryText(0, 1), u"1 file"_s);
    QCOMPARE(FolderModel::summaryText(0, 0), u"No items"_s);

    // The fixture: folders D and E, files a.txt and b.md; 0 s, 2 days, 21 days and 0 s old.
    UiHarness ui;
    const QDir root(ui.root());
    QVERIFY(root.mkpath(u"D"_s));
    QVERIFY(root.mkpath(u"E"_s));
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "a"));
    QVERIFY(writeFile(root.filePath(u"b.md"_s), "b"));
    QVERIFY(age(root.filePath(u"D"_s), 0));
    QVERIFY(age(root.filePath(u"E"_s), 2 * 86400));
    QVERIFY(age(root.filePath(u"a.txt"_s), 21 * 86400));
    QVERIFY(age(root.filePath(u"b.md"_s), 0));

    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"E"_s, u"a.txt"_s, u"b.md"_s}));

    // One outlined, rounded box, in the theme's border and radius.
    QObject *theme = themeOf(ui);
    QVERIFY(theme);
    QQuickItem *table = ui.view(u"repoTable"_s);
    QVERIFY(table);
    QObject *border = table->property("border").value<QObject *>();
    QVERIFY(border);
    QVERIFY(border->property("width").toReal() >= 1);
    QCOMPARE(border->property("color").value<QColor>(), theme->property("tableBorder").value<QColor>());
    QCOMPARE(table->property("radius").toReal(), theme->property("radius").toReal());
    QCOMPARE(table->property("color").value<QColor>().alpha(), 0);

    // Its top band sums up the folder; the rows follow inside the box.
    QQuickItem *band = ui.view(u"tableHeader"_s);
    QVERIFY(band);
    QQuickItem *summary = child(band, u"tableSummary"_s);
    QVERIFY(summary);
    QCOMPARE(summary->property("text").toString(), u"2 folders, 2 files"_s);
    const qreal rowHeight = theme->property("rowHeight").toReal();
    QCOMPARE(table->height(), band->height() + 4 * rowHeight);
    const QPointF tableTop = table->mapToScene(QPointF(0, 0));
    QCOMPARE(band->mapToScene(QPointF(0, 0)), tableTop);
    QQuickItem *lastRow = UiHarness::delegateAt(list, 3);
    QVERIFY(lastRow);
    QCOMPARE(lastRow->mapToScene(QPointF(0, lastRow->height())).y(), tableTop.y() + table->height());

    // Each row: its icon, its name, and on the right how long ago it changed.
    const QStringList ages{u"now"_s, u"2 days ago"_s, u"3 weeks ago"_s, u"now"_s};
    const QStringList categories{u"folder"_s, u"folder"_s, u"text"_s, u"text"_s};
    for (int row = 0; row < 4; ++row) {
        QQuickItem *delegate = UiHarness::delegateAt(list, row);
        QVERIFY(delegate);
        const QString name = delegate->property("name").toString();
        QVERIFY2(shownTexts(delegate).contains(name), qPrintable(name));
        QQuickItem *ageItem = child(delegate, u"age"_s);
        QVERIFY(ageItem && ageItem->isVisible());
        QCOMPARE(ageItem->property("text").toString(), ages.at(row));
        QCOMPARE(ageItem->property("horizontalAlignment").toInt(), int(Qt::AlignRight));
        const qreal right = ageItem->mapToItem(delegate, QPointF(ageItem->width(), 0)).x();
        QVERIFY2(delegate->width() - right <= theme->property("spacing").toReal() * 2,
                 qPrintable(QString::number(delegate->width() - right)));
        QVERIFY(ageItem->x() > delegate->width() / 2);
        // The icon: drawn for its kind, before the name.
        QQuickItem *icon = nullptr;
        const auto children = delegate->childItems();
        for (QQuickItem *item : children) {
            if (item->isVisible() && item->property("category").isValid())
                icon = item;
        }
        QVERIFY2(icon, qPrintable(name));
        QCOMPARE(icon->property("category").toString(), categories.at(row));
        QVERIFY(icon->width() > 0 && icon->x() < ageItem->x());
    }
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void RepoUiTests::readmeRenderedBelow()
{
    UiHarness ui;
    const QDir root(ui.root());
    QVERIFY(writeFile(root.filePath(u"README.md"_s),
                      "# Title\n\nSome *text* and a [link](https://example.org).\n\n- one\n- two\n\n```\ncode()\n```\n"));
    QVERIFY(writeFile(root.filePath(u"readme.md"_s), "# Lower\n"));
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "a"));
    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"README.md"_s, u"readme.md"_s}));

    // The panel, below the table, named after its file.
    QTRY_VERIFY(ui.view(u"readme"_s));
    QQuickItem *panel = ui.view(u"readme"_s);
    QQuickItem *title = child(panel, u"readmeTitle"_s);
    QVERIFY(title);
    QTRY_COMPARE(title->property("text").toString(), u"README.md"_s);
    QQuickItem *table = ui.view(u"repoTable"_s);
    QVERIFY(table);
    QVERIFY(panel->mapToScene(QPointF(0, 0)).y() >= table->mapToScene(QPointF(0, table->height())).y());
    QObject *border = panel->property("border").value<QObject *>();
    QVERIFY(border && border->property("width").toReal() >= 1);

    // The Markdown rendered: what the panel draws.
    QQuickItem *text = child(panel, u"readmeText"_s);
    QVERIFY(text);
    QTextDocument *document = documentOf(text);
    QVERIFY(document);
    QTRY_VERIFY(document->toPlainText().contains(u"Title"_s));
    const QString plain = document->toPlainText();
    for (const QString &mark : {u"#"_s, u"*"_s, u"```"_s, u"]("_s})
        QVERIFY2(!plain.contains(mark), qPrintable(mark + u" in: "_s + plain));
    for (const QString &word : {u"Title"_s, u"Some"_s, u"text"_s, u"link"_s, u"one"_s, u"two"_s, u"code()"_s})
        QVERIFY2(plain.contains(word), qPrintable(word + u" not in: "_s + plain));

    // "Title" a heading, drawn larger than the body text.
    const QTextFragment heading = fragmentOf(document, u"Title"_s);
    QVERIFY(heading.isValid());
    QCOMPARE(document->findBlock(heading.position()).blockFormat().headingLevel(), 1);
    const QTextFragment body = fragmentOf(document, u"Some "_s);
    QVERIFY(body.isValid());
    const qreal headingLine = lineHeightAt(text, heading.position() + 1);
    const qreal bodyLine = lineHeightAt(text, body.position() + 1);
    QVERIFY2(headingLine > bodyLine * 1.2, qPrintable(u"%1 against %2"_s.arg(headingLine).arg(bodyLine)));
    // "text" in italics, and only it.
    const QTextFragment italic = fragmentOf(document, u"text"_s);
    QVERIFY(italic.isValid());
    QVERIFY(italic.charFormat().fontItalic());
    QVERIFY(!body.charFormat().fontItalic());
    // The list's items in a list.
    for (const QString &item : {u"one"_s, u"two"_s}) {
        const QTextFragment fragment = fragmentOf(document, item);
        QVERIFY2(fragment.isValid(), qPrintable(item));
        QVERIFY2(document->findBlock(fragment.position()).textList(), qPrintable(item));
    }
    // "code()" in the monospace font.
    QTextFragment code = fragmentOf(document, u"code()"_s);
    QVERIFY(code.isValid());
    const QTextCharFormat codeFormat = code.charFormat();
    const QStringList families = codeFormat.fontFamilies().toStringList();
    QVERIFY2(codeFormat.fontFixedPitch() || families.contains(u"monospace"_s, Qt::CaseInsensitive),
             qPrintable(families.join(u", "_s)));
    QVERIFY(!body.charFormat().fontFixedPitch());
    // The link: a link.
    QCOMPARE(linksOf(document), QStringList{u"https://example.org"_s});

    // The other names, in their order: README.md, readme.md, README, README.txt.
    QVERIFY(QFile::remove(root.filePath(u"README.md"_s)));
    QTRY_COMPARE(title->property("text").toString(), u"readme.md"_s);
    QTRY_VERIFY(documentOf(text)->toPlainText().contains(u"Lower"_s));
    QVERIFY(writeFile(root.filePath(u"README.txt"_s), "# not a heading\n  *as it is*\n"));
    QVERIFY(QFile::remove(root.filePath(u"readme.md"_s)));
    QTRY_COMPARE(title->property("text").toString(), u"README.txt"_s);
    // Plain text, shown as it is, in the monospace font.
    QTRY_VERIFY(documentOf(text)->toPlainText().contains(u"# not a heading\n  *as it is*"_s));
    QVERIFY(writeFile(root.filePath(u"README"_s), "plain readme"));
    QTRY_COMPARE(title->property("text").toString(), u"README"_s);
    QTRY_VERIFY(documentOf(text)->toPlainText().contains(u"plain readme"_s));
    const QTextFragment pre = fragmentOf(documentOf(text), u"plain readme"_s);
    QVERIFY(pre.isValid());
    QVERIFY(pre.charFormat().fontFixedPitch()
            || pre.charFormat().fontFamilies().toStringList().contains(u"monospace"_s, Qt::CaseInsensitive));

    // A folder without one shows no panel.
    QVERIFY(QFile::remove(root.filePath(u"README"_s)));
    QVERIFY(QFile::remove(root.filePath(u"README.txt"_s)));
    QTRY_VERIFY(!ui.view(u"readme"_s));
    QVERIFY(root.mkpath(u"D"_s));
    QVERIFY(writeFile(root.filePath(u"D/README.md"_s), "# In D\n"));
    ui.browser().open(root.filePath(u"D"_s));
    QTRY_VERIFY(ui.view(u"readme"_s));
    QTRY_VERIFY(documentOf(child(ui.view(u"readme"_s), u"readmeText"_s))->toPlainText().contains(u"In D"_s));
    ui.browser().goBack();
    QTRY_COMPARE(ui.browser().folder(), root.path());
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"D"_s, u"a.txt"_s}));
    QTest::qWait(300);
    QVERIFY(!ui.view(u"readme"_s));
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void RepoUiTests::readmeIsSafe()
{
    // A listener on this machine: an image or anything else fetched from the README's addresses
    // would knock here.
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost));
    int knocks = 0;
    connect(&listener, &QTcpServer::newConnection, &listener, [&] {
        while (QTcpSocket *socket = listener.nextPendingConnection()) {
            ++knocks;
            socket->deleteLater();
        }
    });
    const QByteArray web = "http://127.0.0.1:" + QByteArray::number(listener.serverPort());

    UrlCatcher desktop;
    QDesktopServices::setUrlHandler(u"https"_s, &desktop, "open");
    QDesktopServices::setUrlHandler(u"http"_s, &desktop, "open");
    QDesktopServices::setUrlHandler(u"file"_s, &desktop, "open");
    QDesktopServices::setUrlHandler(u"javascript"_s, &desktop, "open");

    UiHarness ui;
    const QDir root(ui.root());
    QVERIFY(root.mkpath(u"D"_s));
    QVERIFY(writeFile(root.filePath(u"pic.png"_s), QByteArray()));
    QImage picture(4, 4, QImage::Format_RGB32);
    picture.fill(Qt::gray);
    QVERIFY(picture.save(root.filePath(u"pic.png"_s)));
    QVERIFY(writeFile(root.filePath(u"README.md"_s),
                      "<script>alert(1)</script>\n\n"
                      "<img src=http://example.org/x.png>\n\n"
                      "[x](javascript:alert(1))\n\n"
                      "[y](file:///etc/passwd)\n\n"
                      "[z](https://example.org/z) and [h](http://example.org/h) and [d](D) and [r](/etc)\n\n"
                      "![web](" + web + "/web.png) ![local](pic.png)\n\n"
                      "<img src=\"" + web + "/raw.png\">\n\n"
                      "<a href=\"javascript:alert(2)\">w</a>\n"));
    QVERIFY(ui.start({ui.root()}));
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"readme"_s));
    QQuickItem *text = child(ui.view(u"readme"_s), u"readmeText"_s);
    QVERIFY(text);
    QTRY_VERIFY(documentOf(text) && documentOf(text)->toPlainText().contains(u"alert(1)"_s));
    QTextDocument *document = documentOf(text);

    // No HTML interpreted: the tags are text.
    const QString plain = document->toPlainText();
    QVERIFY2(plain.contains(u"<script>alert(1)</script>"_s), qPrintable(plain));
    QVERIFY2(plain.contains(u"<img src=http://example.org/x.png>"_s), qPrintable(plain));
    QVERIFY2(plain.contains(u"<a href=\"javascript:alert(2)\">"_s), qPrintable(plain));

    // Only http, https and relative links are links.
    QCOMPARE(linksOf(document), (QStringList{u"https://example.org/z"_s, u"http://example.org/h"_s, u"D"_s}));
    for (const QString &word : {u"x"_s, u"y"_s, u"r"_s})
        QVERIFY2(!wordIn(document, word).isNull() && !isLink(document, word), qPrintable(word));
    for (const QString &word : {u"z"_s, u"h"_s, u"d"_s})
        QVERIFY2(isLink(document, word), qPrintable(word));

    // Images only from relative local paths: the web one is not even in the document.
    const QStringList images = imagesOf(document);
    QCOMPARE(images, QStringList{QUrl::fromLocalFile(root.filePath(u"pic.png"_s)).toString()});

    // Clicking the https link opens it through the desktop.
    const QPointF https = pointOf(text, u"z"_s);
    QVERIFY(https.x() >= 0);
    click(ui, text, https);
    QTRY_COMPARE(desktop.urls, QList<QUrl>{QUrl(u"https://example.org/z"_s)});
    // Clicking the others opens nothing.
    for (const QString &word : {u"x"_s, u"y"_s, u"r"_s}) {
        const QPointF at = pointOf(text, word);
        QVERIFY2(at.x() >= 0, qPrintable(word));
        click(ui, text, at);
    }
    // Asked straight, the browser refuses them too.
    for (const QString &href : {u"javascript:alert(1)"_s, u"file:///etc/passwd"_s, u"/etc"_s, u"//example.org/x"_s,
                                u"data:text/html,x"_s})
        ui.browser().openReadmeLink(href);
    QTest::qWait(300);
    QCOMPARE(desktop.urls, QList<QUrl>{QUrl(u"https://example.org/z"_s)});
    QCOMPARE(ui.browser().folder(), root.path());

    // Nothing was fetched from the network.
    QTest::qWait(700);
    QCOMPARE(knocks, 0);
    QVERIFY(!listener.hasPendingConnections());

    // A relative link to a folder opens it here.
    const QPointF folderLink = pointOf(text, u"d"_s);
    QVERIFY(folderLink.x() >= 0);
    click(ui, text, folderLink);
    QTRY_COMPARE(ui.browser().folder(), root.filePath(u"D"_s));
    QCOMPARE(desktop.urls.size(), 1);

    QDesktopServices::unsetUrlHandler(u"https"_s);
    QDesktopServices::unsetUrlHandler(u"http"_s);
    QDesktopServices::unsetUrlHandler(u"file"_s);
    QDesktopServices::unsetUrlHandler(u"javascript"_s);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void RepoUiTests::breadcrumbPath()
{
    UiHarness ui;
    // The home folder is the scratch folder, so the path starts at Home.
    const QByteArray homeBefore = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(ui.scratch()));
    const QDir root(ui.root());
    QVERIFY(root.mkpath(u"D/sub"_s));
    QVERIFY(writeFile(root.filePath(u"D/sub/f.txt"_s), "f"));
    QVERIFY(ui.start({root.filePath(u"D/sub"_s)}));
    QQuickItem *crumbs = ui.view(u"breadcrumb"_s);
    QVERIFY(crumbs);
    QTRY_COMPARE(shownTexts(crumbs).join(QString()), u"Home / root / D / sub"_s);
    QVERIFY(shownTexts(crumbs).join(QString()).endsWith(u"root / D / sub"_s));

    // Each part can be clicked: "D" opens D.
    QQuickItem *d = nullptr;
    for (int n = 0; n < 4; ++n) {
        QQuickItem *part = child(crumbs, u"crumb"_s + QString::number(n));
        QVERIFY(part && part->isVisible());
        if (part->property("text").toString() == u"D")
            d = part;
    }
    QVERIFY(d);
    clickMiddle(ui, d);
    QTRY_COMPARE(ui.browser().folder(), root.filePath(u"D"_s));
    QTRY_COMPARE(shownTexts(crumbs).join(QString()), u"Home / root / D"_s);
    QTRY_COMPARE(UiHarness::shownNames(ui.view(u"list"_s)), QStringList{u"sub"_s});
    clickMiddle(ui, child(crumbs, u"crumb1"_s));
    QTRY_COMPARE(ui.browser().folder(), root.path());

    // Ctrl+L still turns it into the field to type a location in.
    ui.press(Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"location"_s));
    QCOMPARE(ui.view(u"location"_s)->property("text").toString(), root.path());
    QVERIFY(!ui.view(u"breadcrumb"_s));
    ui.press(Qt::Key_Escape);
    QTRY_VERIFY(!ui.view(u"location"_s));
    QTRY_VERIFY(ui.view(u"breadcrumb"_s));
    ui.press(Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(ui.view(u"location"_s));
    ui.type(root.filePath(u"D/sub"_s));
    ui.press(Qt::Key_Return);
    QTRY_COMPARE(ui.browser().folder(), root.filePath(u"D/sub"_s));
    QTRY_COMPARE(shownTexts(ui.view(u"breadcrumb"_s)).join(QString()), u"Home / root / D / sub"_s);

    // Outside the home folder the path starts at the root of the file system.
    ui.browser().open(u"/usr"_s);
    QTRY_COMPARE(shownTexts(ui.view(u"breadcrumb"_s)).join(QString()), u"/usr"_s);
    qputenv("HOME", homeBefore);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void RepoUiTests::gitFolderShowsLastCommits()
{
    Riley::GitLog::clearCache();
    UiHarness ui;
    const QString repo = ui.root();
    const QDir root(repo);
    // a.txt committed as "Add a" two days ago, b.txt as "Fix b" now.
    QVERIFY(!git(repo, {u"init"_s, u"-q"_s, u"-b"_s, u"main"_s}).isNull());
    QVERIFY(writeFile(root.filePath(u"a.txt"_s), "a"));
    QVERIFY(!git(repo, {u"add"_s, u"a.txt"_s}).isNull());
    QVERIFY(!git(repo, {u"commit"_s, u"-q"_s, u"-m"_s, u"Add a"_s}, 2 * 86400).isNull());
    QVERIFY(writeFile(root.filePath(u"b.txt"_s), "b"));
    QVERIFY(!git(repo, {u"add"_s, u"b.txt"_s}).isNull());
    QVERIFY(!git(repo, {u"commit"_s, u"-q"_s, u"-m"_s, u"Fix b"_s}).isNull());

    QVERIFY(ui.start({repo}));
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s}));

    // Each row: the subject of its last commit, and how long ago that was.
    QTRY_COMPARE(textOf(child(rowNamed(list, u"a.txt"_s), u"commit"_s)), u"Add a"_s);
    QQuickItem *a = rowNamed(list, u"a.txt"_s);
    QVERIFY(child(a, u"commit"_s)->isVisible());
    QCOMPARE(textOf(child(a, u"age"_s)), u"2 days ago"_s);
    QQuickItem *b = rowNamed(list, u"b.txt"_s);
    QCOMPARE(textOf(child(b, u"commit"_s)), u"Fix b"_s);
    QCOMPARE(textOf(child(b, u"age"_s)), u"now"_s);
    // Between the name and the date.
    QQuickItem *commitA = child(a, u"commit"_s);
    QVERIFY(commitA->x() > 0 && commitA->x() + commitA->width() <= child(a, u"age"_s)->x());
    QVERIFY(shownTexts(a).contains(u"a.txt"_s));

    // The header: the branch and the latest commit.
    QQuickItem *band = ui.view(u"tableHeader"_s);
    QVERIFY(band);
    QTRY_VERIFY(ui.view(u"tableBranch"_s));
    QCOMPARE(textOf(ui.view(u"tableBranch"_s)), u"main"_s);
    QVERIFY(ui.view(u"tableCommit"_s));
    QCOMPARE(textOf(ui.view(u"tableCommit"_s)), u"Fix b"_s);
    QCOMPARE(textOf(ui.view(u"tableCommitAuthor"_s)), u"Ada Tester"_s);
    QCOMPARE(textOf(ui.view(u"tableCommitAge"_s)), u"now"_s);
    const QStringList header = shownTexts(band);
    QVERIFY2(header.contains(u"main"_s) && header.contains(u"Fix b"_s), qPrintable(header.join(u" | "_s)));
    QCOMPARE(textOf(child(band, u"tableSummary"_s)), u"2 files"_s);

    // A name a shell would take apart, committed while the window is open: its row shows it.
    const QString odd = u"it's $HOME; x.txt"_s;
    QVERIFY(writeFile(root.filePath(odd), "odd"));
    QVERIFY(!git(repo, {u"add"_s, u"--"_s, odd}).isNull());
    QVERIFY(!git(repo, {u"commit"_s, u"-q"_s, u"-m"_s, u"Odd name"_s}).isNull());
    QTRY_COMPARE(UiHarness::shownNames(list), (QStringList{u"a.txt"_s, u"b.txt"_s, odd}));
    QTRY_COMPARE(textOf(child(rowNamed(list, odd), u"commit"_s)), u"Odd name"_s);
    QTRY_COMPARE(textOf(ui.view(u"tableCommit"_s)), u"Odd name"_s);
    QCOMPARE(textOf(child(rowNamed(list, u"a.txt"_s), u"commit"_s)), u"Add a"_s);

    // git was run as itself with a list of arguments, never through a shell.
    const QList<Riley::GitLog::Run> runs = Riley::GitLog::runs();
    QVERIFY(!runs.isEmpty());
    for (const Riley::GitLog::Run &run : runs)
        QCOMPARE(run.program, u"git"_s);

    // A folder in a subfolder of the repository: the same history, from there.
    QVERIFY(root.mkpath(u"D/sub"_s));
    QVERIFY(writeFile(root.filePath(u"D/sub/c.txt"_s), "c"));
    QVERIFY(!git(repo, {u"add"_s, u"D"_s}).isNull());
    QVERIFY(!git(repo, {u"commit"_s, u"-q"_s, u"-m"_s, u"Add c"_s}, 3600).isNull());
    QTRY_COMPARE(textOf(child(rowNamed(list, u"D"_s), u"commit"_s)), u"Add c"_s);
    QCOMPARE(textOf(child(rowNamed(list, u"D"_s), u"age"_s)), u"1 hour ago"_s);
    ui.browser().open(root.filePath(u"D"_s));
    QTRY_COMPARE(UiHarness::shownNames(list), QStringList{u"sub"_s});
    QTRY_COMPARE(textOf(child(rowNamed(list, u"sub"_s), u"commit"_s)), u"Add c"_s);
    QTRY_COMPARE(textOf(ui.view(u"tableCommit"_s)), u"Add c"_s);

    // A folder that is not in a repository: no commit column, no branch.
    const QString plain = ui.scratch() + u"/plain"_s;
    QVERIFY(QDir().mkpath(plain));
    QVERIFY(writeFile(plain + u"/p.txt"_s, "p"));
    ui.browser().open(plain);
    QTRY_COMPARE(UiHarness::shownNames(list), QStringList{u"p.txt"_s});
    QTest::qWait(300);
    QQuickItem *p = rowNamed(list, u"p.txt"_s);
    QVERIFY(p);
    QVERIFY(!child(p, u"commit"_s)->isVisible());
    QVERIFY(textOf(child(p, u"commit"_s)).isEmpty());
    QVERIFY(!ui.view(u"tableBranch"_s));
    QVERIFY(!ui.view(u"tableCommit"_s));
    QCOMPARE(textOf(ui.view(u"tableSummary"_s)), u"1 file"_s);
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

void RepoUiTests::gitInfoStaysOffTheUiThread()
{
    // 2,000 files in 2,000 commits, made in one go by git fast-import.
    constexpr int Files = 2000;
    Riley::GitLog::clearCache();
    UiHarness ui;
    const QString repo = ui.root();
    QVERIFY(!git(repo, {u"init"_s, u"-q"_s, u"-b"_s, u"main"_s}).isNull());
    // Every commit at least 3 days old, the newest last.
    const qint64 base = QDateTime::currentSecsSinceEpoch() - 3 * 86400 - Files - 60;
    QByteArray stream;
    for (int n = 1; n <= Files; ++n) {
        const QByteArray name = "f" + QByteArray::number(n).rightJustified(4, '0');
        const QByteArray message = "Add " + name;
        const QByteArray when = QByteArray::number(base + n) + " +0000";
        stream += "commit refs/heads/main\nmark :" + QByteArray::number(n) + "\n";
        stream += "author Ada Tester <ada@example.org> " + when + "\n";
        stream += "committer Ada Tester <ada@example.org> " + when + "\n";
        stream += "data " + QByteArray::number(message.size()) + "\n" + message + "\n";
        if (n > 1)
            stream += "from :" + QByteArray::number(n - 1) + "\n";
        stream += "M 100644 inline " + name + ".txt\ndata 2\nx\n\n";
    }
    QVERIFY(!git(repo, {u"fast-import"_s, u"--quiet"_s}, 0, stream).isNull());
    QVERIFY(!git(repo, {u"reset"_s, u"-q"_s, u"--hard"_s, u"main"_s}).isNull());
    QCOMPARE(QDir(repo).entryList(QDir::Files).size(), Files);

    QVERIFY(ui.start({repo}));
    ui.press(Qt::Key_2, Qt::ControlModifier);
    QQuickItem *list = ui.view(u"list"_s);
    QVERIFY(list);

    // The window keeps drawing while git works: frames asked for every 10 ms.
    QQuickWindow *w = ui.window();
    QElapsedTimer clock;
    clock.start();
    QList<qint64> frames;
    const QMetaObject::Connection counting = QObject::connect(
        w, &QQuickWindow::frameSwapped, w, [&] { frames.append(clock.elapsed()); }, Qt::DirectConnection);
    QTimer ask;
    QObject::connect(&ask, &QTimer::timeout, w, &QQuickWindow::update);
    ask.start(10);

    Riley::FolderModel *model = ui.browser().model();
    const auto filled = [model] {
        if (model->rowCount() != Files)
            return false;
        for (int row = 0; row < Files; ++row) {
            const QModelIndex index = model->index(row);
            const QString name = index.data(Riley::FolderModel::NameRole).toString();
            if (index.data(Riley::FolderModel::CommitSubjectRole).toString() != u"Add "_s + name.chopped(4))
                return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(filled(), 5000);
    const qint64 took = clock.elapsed();
    QTest::qWait(100);
    ask.stop();
    QObject::disconnect(counting);
    QVERIFY2(took <= 5000, qPrintable(u"the commit column took %1 ms"_s.arg(took)));
    qint64 longest = frames.isEmpty() ? 0 : frames.first();
    for (qsizetype i = 1; i < frames.size(); ++i)
        longest = std::max(longest, frames.at(i) - frames.at(i - 1));
    QVERIFY2(frames.size() > 5, qPrintable(QString::number(frames.size())));
    QVERIFY2(longest <= 250, qPrintable(u"the window stalled %1 ms"_s.arg(longest)));

    // The rows on screen show it, the newest commit heads the table.
    QQuickItem *first = UiHarness::delegateAt(list, 0);
    QVERIFY(first);
    QTRY_COMPARE(textOf(child(first, u"commit"_s)), u"Add f0001"_s);
    QCOMPARE(textOf(child(first, u"age"_s)), u"3 days ago"_s);
    QTRY_COMPARE(textOf(ui.view(u"tableCommit"_s)), u"Add f2000"_s);

    // git ran as itself, with arguments, and never on the UI thread.
    const QList<Riley::GitLog::Run> runs = Riley::GitLog::runs();
    QVERIFY(!runs.isEmpty());
    bool logged = false;
    for (const Riley::GitLog::Run &run : runs) {
        QCOMPARE(run.program, u"git"_s);
        logged = logged || run.arguments.contains(u"log"_s);
    }
    QVERIFY(logged);
    const QSet<Qt::HANDLE> threads = Riley::GitLog::threads();
    QVERIFY(!threads.isEmpty());
    QVERIFY2(!threads.contains(QThread::currentThreadId()), "git ran on the UI thread");
    QVERIFY2(ui.qmlWarnings().isEmpty(), qPrintable(ui.qmlWarnings().join(u'\n')));
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    RepoUiTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}

#include "repoui_tests.moc"
