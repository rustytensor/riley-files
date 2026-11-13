// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileyindex_export.h"

#include <QString>

namespace Riley {

// How far the index is, read from its database file (no service needed).
struct RILEYINDEX_EXPORT IndexStatus
{
    bool exists = false;    // there is an index file
    QString state;          // "empty", "scanning" or "done"
    bool paused = false;
    qint64 entries = 0;     // files and folders
    qint64 files = 0;       // files only
    qint64 bytes = 0;       // the database with its write-ahead log

    // Opens the file read-only for as long as it takes to count. Safe on any thread.
    static IndexStatus read(const QString &databaseFile);
};

// The window's side of the indexer service (riley-files-indexer): one line sent on the service's
// socket, one line back. Commands: "status", "pause", "resume", "rebuild", "reload" (the
// settings file changed).
class RILEYINDEX_EXPORT IndexerClient
{
public:
    // RILEY_INDEXER_SOCKET when set, otherwise $XDG_RUNTIME_DIR/riley-files/indexer.sock.
    static QString defaultSocket();

    // Sends `command`. With `answer`, waits up to `timeoutMs` for the service's line. False when
    // no service listens there.
    static bool send(const QString &command, QString *answer = nullptr, int timeoutMs = 1000,
                     const QString &socket = QString());
};

} // namespace Riley
