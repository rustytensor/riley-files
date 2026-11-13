// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "uicheck.h"

#include "browser.h"
#include "foldermodel.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <atomic>
#include <cstdio>

namespace {

// How long the window may take to show its first frame before the check gives up. Well inside
// the 10 s a build check allows.
constexpr int firstFrameTimeoutMs = 5000;

std::atomic<int> qmlWarnings{0};
QtMessageHandler previousHandler = nullptr;

bool isQmlCategory(const char *category)
{
    if (!category)
        return false;
    const QByteArrayView name(category);
    return name == "qml" || name == "js" || name.startsWith("qt.qml") || name.startsWith("qt.quick");
}

// Counts warnings from QML and Qt Quick, then passes every message on unchanged so the user
// still sees it.
void countingHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg && isQmlCategory(context.category))
        ++qmlWarnings;
    if (previousHandler) {
        previousHandler(type, context, message);
        return;
    }
    // No handler was installed before ours, so print the way Qt does by default.
    const QByteArray line = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", line.constData());
}

} // namespace

UiCheck::UiCheck()
{
    m_clock.start();
    // Draw in software: the check is about the window loading, and should not depend on the
    // machine having a GPU or a display.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    previousHandler = qInstallMessageHandler(countingHandler);
}

UiCheck::~UiCheck()
{
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
}

void UiCheck::watch(QQmlApplicationEngine &engine)
{
    // Once something listens to these, the engine may stop printing them, so the check prints
    // them itself (and turns the engine's own printing off, so none appears twice).
    engine.setOutputWarningsToStandardError(false);
    connect(&engine, &QQmlEngine::warnings, this, [](const QList<QQmlError> &list) {
        for (const QQmlError &error : list) {
            ++qmlWarnings;
            std::fprintf(stderr, "%s\n", error.toString().toLocal8Bit().constData());
        }
    });
}

int UiCheck::run(QQmlApplicationEngine &engine)
{
    const auto roots = engine.rootObjects();
    auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    if (!window) {
        std::fputs("riley-files: the main window did not load\n", stderr);
        return 1;
    }

    // The first finished frame means the window was shown with all its content in place.
    connect(window, &QQuickWindow::frameSwapped, this, [this, window] { frameDone(window); },
            Qt::QueuedConnection);
    QTimer::singleShot(firstFrameTimeoutMs, this, [this, window] { finish(window, false); });

    return QGuiApplication::exec();
}

void UiCheck::frameDone(QQuickWindow *window)
{
    if (m_done)
        return;
    if (m_browser) {
        // A frame counts once the folder is read and its files were in the window it drew.
        const Riley::FolderModel *model = m_browser->model();
        if (model->isLoading()) {
            window->update();
            return;
        }
        if (!m_folderSeen) {
            // The files are in the model; the next frame is the first that shows them.
            m_folderSeen = true;
            window->update();
            return;
        }
        std::printf("first-frame %lld ms, %d files\n", static_cast<long long>(m_clock.elapsed()),
                    model->count());
        std::fflush(stdout);
    }
    finish(window, true);
}

void UiCheck::finish(QQuickWindow *window, bool shown)
{
    if (m_done)
        return;
    m_done = true;

    int code = 0;
    if (!shown || !window->isVisible()) {
        std::fputs("riley-files: the main window was not shown in time\n", stderr);
        code = 2;
    } else if (const int count = qmlWarnings.load(); count > 0) {
        std::fprintf(stderr, "riley-files: QML reported %d warning(s) while loading\n", count);
        code = 3;
    }
    QCoreApplication::exit(code);
}
