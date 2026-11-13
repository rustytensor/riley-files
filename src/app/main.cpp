// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "appwindow.h"
#include "browser.h"
#include "commandline.h"
#include "uicheck.h"

#include <QGuiApplication>

#include <functional>
#include <memory>
#include <vector>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("riley-files"));
    QGuiApplication::setApplicationVersion(QStringLiteral(RILEY_FILES_VERSION));
    // Matches riley-files.desktop, so the compositor pairs the window with its icon and name.
    QGuiApplication::setDesktopFileName(QStringLiteral("riley-files"));

    const CommandLine line = CommandLine::parse(QGuiApplication::arguments());

    // Must exist before the engine so that no warning during loading is missed.
    std::unique_ptr<UiCheck> check;
    if (line.checkUi)
        check = std::make_unique<UiCheck>();

    AppWindow window;
    QQmlApplicationEngine &engine = window.engine();
    if (check)
        check->watch(engine);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    // File > New Window: another window on the same folder, with its own browser. Closing the
    // last one quits.
    std::vector<std::unique_ptr<AppWindow>> more;
    std::function<void(Browser &)> offerNewWindows = [&](Browser &browser) {
        QObject::connect(&browser, &Browser::newWindowAsked, &app, [&](const QString &folder) {
            auto another = std::make_unique<AppWindow>();
            offerNewWindows(another->browser());
            another->load(folder);
            more.push_back(std::move(another));
        }, Qt::QueuedConnection);
    };
    offerNewWindows(window.browser());
    window.load(line.startLocation());

    if (check)
        return check->run(engine);
    if (!window.window())
        return 1;
    return app.exec();
}
