// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QString>
#include <QStringList>

// What the program was asked to do when started.
struct CommandLine
{
    bool checkUi = false;    // load the window, wait for its first frame, exit
    QStringList locations;   // folders, files or file:// URLs to open, in order

    // The first location, or empty for the home folder.
    QString startLocation() const { return locations.isEmpty() ? QString() : locations.first(); }

    // Reads `arguments` (the program name first). --help and --version print and exit here, as
    // does an option the program does not know.
    static CommandLine parse(const QStringList &arguments);
};
