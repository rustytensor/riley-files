// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "commandline.h"

#include <QCommandLineParser>
#include <QCoreApplication>

CommandLine CommandLine::parse(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("CommandLine", "Files, the file manager of the Riley desktop"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption checkUiOption(
        QStringLiteral("check-ui"),
        QCoreApplication::translate("CommandLine", "Load the main window, wait until it is shown, then exit. "
                                                   "Exits non-zero if the window does not load or QML warns."));
    parser.addOption(checkUiOption);
    const QCommandLineOption timeStartOption(
        QStringLiteral("time-start"),
        QCoreApplication::translate("CommandLine", "Load the main window, wait for the first frame that shows "
                                                   "the folder's files, print how long that took, then exit."));
    parser.addOption(timeStartOption);
    // The desktop file passes folders or URLs to open. One window shows the first; tabs for the
    // others come later.
    parser.addPositionalArgument(QStringLiteral("locations"),
                                 QCoreApplication::translate("CommandLine", "Folders to open."),
                                 QCoreApplication::translate("CommandLine", "[locations...]"));
    parser.process(arguments);

    CommandLine line;
    line.timeStart = parser.isSet(timeStartOption);
    line.checkUi = parser.isSet(checkUiOption) || line.timeStart;
    line.locations = parser.positionalArguments();
    return line;
}
