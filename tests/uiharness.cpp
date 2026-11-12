// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "uiharness.h"

#include "appwindow.h"
#include "browser.h"
#include "commandline.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

namespace {

QMutex warningsLock;
QStringList warnings;
QtMessageHandler previousHandler = nullptr;
int harnesses = 0;

bool isQmlCategory(const char *category)
{
    if (!category)
        return false;
    const QByteArrayView name(category);
    return name == "qml" || name == "js" || name.startsWith("qt.qml") || name.startsWith("qt.quick");
}

void keepWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg && isQmlCategory(context.category)) {
        QMutexLocker locker(&warningsLock);
        warnings.append(message);
    }
    if (previousHandler)
        previousHandler(type, context, message);
}

QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    const auto children = parent->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

bool visibleOnScreen(const QQuickItem *item)
{
    for (const QQuickItem *at = item; at; at = at->parentItem()) {
        if (!at->isVisible() || at->opacity() <= 0.0)
            return false;
    }
    return item->width() > 0 && item->height() > 0;
}

} // namespace

UiHarness::UiHarness()
{
    // Drawn in software, so the tests need no GPU or display. Set once: the choice holds for
    // the whole run.
    static const bool software = [] {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        return true;
    }();
    Q_UNUSED(software);
    if (harnesses++ == 0) {
        previousHandler = qInstallMessageHandler(keepWarnings);
    }
    {
        QMutexLocker locker(&warningsLock);
        warnings.clear();
    }
    QDir(m_scratch.path()).mkpath(QStringLiteral("root"));
}

UiHarness::~UiHarness()
{
    stop();
    if (--harnesses == 0) {
        qInstallMessageHandler(previousHandler);
        previousHandler = nullptr;
        QMutexLocker locker(&warningsLock);
        warnings.clear();
    }
}

QString UiHarness::scratch() const
{
    return m_scratch.path();
}

QString UiHarness::root() const
{
    return m_scratch.filePath(QStringLiteral("root"));
}

QString UiHarness::settingsFile() const
{
    return m_scratch.filePath(QStringLiteral("settings.ini"));
}

bool UiHarness::makeTreeFixture() const
{
    return makeFile(QStringLiteral("Alpha/Beta/c.txt")) && makeFile(QStringLiteral("Alpha/d.txt"))
           && makeFile(QStringLiteral("Zeta/z.txt")) && makeFile(QStringLiteral("e.txt"));
}

bool UiHarness::makeFile(const QString &relativePath, qint64 size) const
{
    const QString path = QDir(root()).filePath(relativePath);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(QByteArray(size, 'x')) == size;
}

bool UiHarness::start(const QStringList &arguments)
{
    stop();
    const CommandLine line = CommandLine::parse(QStringList{QStringLiteral("riley-files")} + arguments);

    m_app = std::make_unique<AppWindow>(settingsFile());
    QQmlApplicationEngine &engine = m_app->engine();
    engine.setOutputWarningsToStandardError(false);
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError> &list) {
        QMutexLocker locker(&warningsLock);
        for (const QQmlError &error : list) {
            warnings.append(error.toString());
            qWarning("%s", qPrintable(error.toString()));
        }
    });

    if (!m_app->load(line.startLocation()))
        return false;
    QQuickWindow *w = m_app->window();
    w->show();
    w->requestActivate();
    return QTest::qWaitForWindowExposed(w) && QTest::qWaitForWindowActive(w);
}

void UiHarness::stop()
{
    if (!m_app)
        return;
    if (QQuickWindow *w = m_app->window())
        w->close();
    m_app.reset();
    // Lets deleted items and queued work go before the next window.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QQuickWindow *UiHarness::window() const
{
    return m_app ? m_app->window() : nullptr;
}

Browser &UiHarness::browser() const
{
    return m_app->browser();
}

QQuickItem *UiHarness::item(const QString &objectName) const
{
    QQuickWindow *w = window();
    return w ? findItem(w->contentItem(), objectName) : nullptr;
}

QQuickItem *UiHarness::view(const QString &objectName) const
{
    QQuickItem *found = item(objectName);
    return found && visibleOnScreen(found) ? found : nullptr;
}

void UiHarness::press(int key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyClick(window(), Qt::Key(key), modifiers);
}

void UiHarness::type(const QString &text)
{
    for (const QChar c : text)
        QTest::keyClick(window(), c.toLatin1());
}

QQuickItem *UiHarness::delegateAt(QQuickItem *view, int row)
{
    QQuickItem *delegate = nullptr;
    if (view)
        QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, delegate), Q_ARG(int, row));
    return delegate;
}

QStringList UiHarness::shownNames(QQuickItem *view)
{
    QStringList names;
    if (!view)
        return names;
    const int count = view->property("count").toInt();
    for (int row = 0; row < count; ++row) {
        QQuickItem *delegate = delegateAt(view, row);
        names.append(delegate ? delegate->property("name").toString() : QStringLiteral("?"));
    }
    return names;
}

QString UiHarness::currentName(QQuickItem *view)
{
    if (!view)
        return {};
    const int row = view->property("currentIndex").toInt();
    QQuickItem *delegate = row >= 0 ? delegateAt(view, row) : nullptr;
    return delegate ? delegate->property("name").toString() : QString();
}

QList<int> UiHarness::shownInts(QQuickItem *view, const char *property)
{
    QList<int> values;
    if (!view)
        return values;
    const int count = view->property("count").toInt();
    for (int row = 0; row < count; ++row) {
        QQuickItem *delegate = delegateAt(view, row);
        values.append(delegate ? delegate->property(property).toInt() : -1);
    }
    return values;
}

QStringList UiHarness::qmlWarnings() const
{
    QMutexLocker locker(&warningsLock);
    return warnings;
}
