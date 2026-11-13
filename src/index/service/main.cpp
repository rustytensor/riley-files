// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// riley-files-indexer: keeps Riley Files' search index (a user service, run by systemd's user
// manager as riley-files-indexer.service). Reads the [index] settings of the program's settings
// file, scans once, then follows the changes the change helper reports. It runs at nice 10 in
// the idle I/O class, so the person's own work always comes first.

#include "indexdatabase.h"
#include "indexerclient.h"
#include "indexsettings.h"
#include "indexservice.h"

#include <QCoreApplication>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

// From the kernel's ioprio interface (linux/ioprio.h).
constexpr int IoprioWhoProcess = 1;
constexpr int IoprioClassIdle = 3;
constexpr int IoprioClassShift = 13;

int signalPipe[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = ::write(signalPipe[1], &byte, 1);
}

// Before any thread exists: threads made later take both over.
void stayOutOfTheWay()
{
    errno = 0;
    const int nice = ::getpriority(PRIO_PROCESS, 0);
    if (errno == 0 && nice < 10)
        ::setpriority(PRIO_PROCESS, 0, 10);
    ::syscall(SYS_ioprio_set, IoprioWhoProcess, 0, IoprioClassIdle << IoprioClassShift);
}

void usage(FILE *to)
{
    std::fprintf(to, "Usage: riley-files-indexer [--settings FILE] [--database FILE] [--socket PATH] [--changes PATH]\n"
                     "Keeps the search index of Riley Files.\n");
}

} // namespace

int main(int argc, char **argv)
{
    stayOutOfTheWay();
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("riley-files-indexer"));

    QString settingsFile = Riley::IndexSettings::defaultFile();
    QString databaseFile = Riley::IndexDatabase::defaultFile();
    QString socket = Riley::IndexerClient::defaultSocket();
    QString changes;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        const bool hasValue = i + 1 < args.size();
        if (arg == QLatin1String("--settings") && hasValue) {
            settingsFile = args.at(++i);
        } else if (arg == QLatin1String("--database") && hasValue) {
            databaseFile = args.at(++i);
        } else if (arg == QLatin1String("--socket") && hasValue) {
            socket = args.at(++i);
        } else if (arg == QLatin1String("--changes") && hasValue) {
            changes = args.at(++i);
        } else if (arg == QLatin1String("--help")) {
            usage(stdout);
            return 0;
        } else {
            usage(stderr);
            return 2;
        }
    }

    std::signal(SIGPIPE, SIG_IGN);
    if (::pipe2(signalPipe, O_CLOEXEC | O_NONBLOCK) != 0)
        return 1;
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);

    Riley::IndexService service(settingsFile, databaseFile, socket, changes);
    int code = 0;
    QObject::connect(&service, &Riley::IndexService::quitAsked, &app, [&code](int c) {
        code = c;
        QCoreApplication::quit();
    });
    QSocketNotifier signalNotifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [] { QCoreApplication::quit(); });

    QString error;
    if (!service.start(error)) {
        std::fprintf(stderr, "riley-files-indexer: %s\n", qPrintable(error));
        return 1;
    }
    app.exec();
    service.shutDown();
    return code;
}
