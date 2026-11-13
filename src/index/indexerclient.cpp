// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "indexerclient.h"

#include "indexdatabase.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Riley {

IndexStatus IndexStatus::read(const QString &databaseFile)
{
    IndexStatus status;
    if (!QFileInfo::exists(databaseFile))
        return status;
    IndexDatabase db;
    if (!db.open(databaseFile, IndexDatabase::ReadOnly))
        return status;
    status.exists = true;
    status.state = db.state();
    status.paused = db.paused();
    status.entries = db.entryCount();
    status.files = db.fileCount();
    status.bytes = db.sizeOnDisk();
    return status;
}

QString IndexerClient::defaultSocket()
{
    const QByteArray given = qgetenv("RILEY_INDEXER_SOCKET");
    if (!given.isEmpty())
        return QFile::decodeName(given);
    QString runtime = QFile::decodeName(qgetenv("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty())
        runtime = QStringLiteral("/run/user/%1").arg(::getuid());
    return runtime + QStringLiteral("/riley-files/indexer.sock");
}

bool IndexerClient::send(const QString &command, QString *answer, int timeoutMs, const QString &socket)
{
    const QByteArray path = QFile::encodeName(socket.isEmpty() ? defaultSocket() : socket);
    sockaddr_un address{};
    if (path.isEmpty() || size_t(path.size()) >= sizeof(address.sun_path))
        return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()));

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    // A local socket connects at once or not at all: nobody listening is a refusal.
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return false;
    }
    const QByteArray line = command.toUtf8() + '\n';
    qsizetype sent = 0;
    while (sent < line.size()) {
        const ssize_t n = ::send(fd, line.constData() + sent, size_t(line.size() - sent), MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        sent += n;
    }
    if (answer) {
        answer->clear();
        QByteArray reply;
        QElapsedTimer clock;
        clock.start();
        while (!reply.contains('\n') && reply.size() < 4096) {
            const qint64 left = timeoutMs - clock.elapsed();
            if (left <= 0)
                break;
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, int(left)) <= 0)
                break;
            char buffer[512];
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0)
                break;
            reply.append(buffer, n);
        }
        *answer = QString::fromUtf8(reply.left(reply.indexOf('\n'))).trimmed();
    }
    ::close(fd);
    return true;
}

} // namespace Riley
