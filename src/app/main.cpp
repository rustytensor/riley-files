// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "uicheck.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <memory>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("riley-files"));
    QGuiApplication::setApplicationVersion(QStringLiteral(RILEY_FILES_VERSION));
    // Matches riley-files.desktop, so the compositor pairs the window with its icon and name.
    QGuiApplication::setDesktopFileName(QStringLiteral("riley-files"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Files, the file manager of the Riley desktop"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption checkUiOption(
        QStringLiteral("check-ui"),
        QStringLiteral("Load the main window, wait until it is shown, then exit. "
                       "Exits non-zero if the window does not load or QML warns."));
    parser.addOption(checkUiOption);
    // The desktop file passes folders or URLs to open; browsing them comes in a later milestone.
    parser.addPositionalArgument(QStringLiteral("locations"), QStringLiteral("Folders to open."),
                                 QStringLiteral("[locations...]"));
    parser.process(app);

    const bool checking = parser.isSet(checkUiOption);

    // Must exist before the engine so that no warning during loading is missed.
    std::unique_ptr<UiCheck> check;
    if (checking)
        check = std::make_unique<UiCheck>();

    QQmlApplicationEngine engine;
    if (check)
        check->watch(engine);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("RileyFiles", "Main");

    if (check)
        return check->run(engine);
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
