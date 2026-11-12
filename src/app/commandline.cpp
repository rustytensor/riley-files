// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "commandline.h"

#include <QCommandLineParser>

CommandLine CommandLine::parse(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Files, the file manager of the Riley desktop"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption checkUiOption(
        QStringLiteral("check-ui"),
        QStringLiteral("Load the main window, wait until it is shown, then exit. "
                       "Exits non-zero if the window does not load or QML warns."));
    parser.addOption(checkUiOption);
    // The desktop file passes folders or URLs to open. One window shows the first; tabs for the
    // others come later.
    parser.addPositionalArgument(QStringLiteral("locations"), QStringLiteral("Folders to open."),
                                 QStringLiteral("[locations...]"));
    parser.process(arguments);

    CommandLine line;
    line.checkUi = parser.isSet(checkUiOption);
    line.locations = parser.positionalArguments();
    return line;
}
