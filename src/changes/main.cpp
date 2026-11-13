// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// riley-files-changes: the change helper. Run as root by systemd
// (riley-files-changes.service); tells Riley Files' indexer what changed in the folders it
// indexes, from fanotify, filtered by what each user may see. It never writes a file except its
// socket and never runs a program.

#include "changeclient.h"
#include "changeserver.h"
#include "fanotifysource.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace {

Riley::ChangeServer *g_server = nullptr;

void onSignal(int)
{
    if (g_server)
        g_server->stop();
}

} // namespace

int main(int argc, char **argv)
{
    std::string socketPath = "/run/riley-files/changes.sock";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (arg == "--help") {
            std::printf("Usage: riley-files-changes [--socket PATH]\n"
                        "Tells Riley Files what changed in the folders it indexes.\n");
            return 0;
        } else {
            std::fprintf(stderr, "riley-files-changes: unknown argument %s\n", argv[i]);
            return 2;
        }
    }

    ::umask(022);
    std::signal(SIGPIPE, SIG_IGN);

    Riley::FanotifySource source;
    std::string error;
    if (!source.isValid(&error)) {
        std::fprintf(stderr, "riley-files-changes: %s\n", error.c_str());
        return 1;
    }
    Riley::ChangeServer server(source, socketPath);
    if (!server.listen(error)) {
        std::fprintf(stderr, "riley-files-changes: %s\n", error.c_str());
        return 1;
    }
    g_server = &server;
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    server.run();
    g_server = nullptr;
    return 0;
}
