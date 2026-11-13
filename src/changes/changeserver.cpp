// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "changeserver.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Riley {

namespace {

constexpr std::size_t kReadPerRound = 64 * 1024; // from one client, before the others get a turn
constexpr int kAcceptsPerRound = 64;

// `path` is `folder` or below it.
bool within(const std::string &path, const std::string &folder)
{
    if (folder == "/")
        return !path.empty() && path[0] == '/';
    return path.size() >= folder.size() && path.compare(0, folder.size(), folder) == 0
           && (path.size() == folder.size() || path[folder.size()] == '/');
}

std::string parentOf(const std::string &path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == 0 || slash == std::string::npos ? std::string("/") : path.substr(0, slash);
}

} // namespace

ChangeServer::ChangeServer(EventSource &source, std::string socketPath)
    : ChangeServer(source, std::move(socketPath), Limits{})
{
}

ChangeServer::ChangeServer(EventSource &source, std::string socketPath, Limits limits, CredentialsOf credentialsOf)
    : m_source(source)
    , m_socketPath(std::move(socketPath))
    , m_limits(limits)
    , m_credentialsOf(credentialsOf ? std::move(credentialsOf) : CredentialsOf(&ChangeServer::peerCredentials))
    , m_wake(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
{
}

ChangeServer::~ChangeServer()
{
    for (Client &client : m_clients)
        if (client.fd >= 0)
            ::close(client.fd);
    if (m_listen >= 0) {
        ::close(m_listen);
        ::unlink(m_socketPath.c_str());
    }
    if (m_wake >= 0)
        ::close(m_wake);
}

bool ChangeServer::peerCredentials(int socket, Credentials &out)
{
    ucred cred{};
    socklen_t size = sizeof cred;
    if (::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &cred, &size) != 0 || size != sizeof cred)
        return false;
    out.uid = cred.uid;
    out.gid = cred.gid;
    out.pid = cred.pid;
    std::vector<gid_t> groups(64);
    for (int attempt = 0; attempt < 2; ++attempt) {
        socklen_t bytes = socklen_t(groups.size() * sizeof(gid_t));
        if (::getsockopt(socket, SOL_SOCKET, SO_PEERGROUPS, groups.data(), &bytes) == 0) {
            groups.resize(bytes / sizeof(gid_t));
            out.groups = std::move(groups);
            return true;
        }
        if (errno != ERANGE || bytes / sizeof(gid_t) > 65536)
            return false;
        groups.resize(bytes / sizeof(gid_t));
    }
    return false;
}

bool ChangeServer::listen(std::string &error)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (m_socketPath.empty() || m_socketPath.size() >= sizeof address.sun_path) {
        error = "socket path too long";
        return false;
    }
    std::memcpy(address.sun_path, m_socketPath.c_str(), m_socketPath.size() + 1);

    struct stat st;
    if (::lstat(m_socketPath.c_str(), &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            error = m_socketPath + " exists and is not a socket";
            return false;
        }
        ::unlink(m_socketPath.c_str());
    }
    m_listen = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (m_listen < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (::bind(m_listen, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0
        || ::chmod(m_socketPath.c_str(), 0666) != 0 || ::listen(m_listen, 64) != 0) {
        error = m_socketPath + ": " + std::strerror(errno);
        ::close(m_listen);
        m_listen = -1;
        return false;
    }
    return true;
}

void ChangeServer::stop()
{
    m_stopped = true;
    const std::uint64_t one = 1;
    [[maybe_unused]] const ssize_t written = ::write(m_wake, &one, sizeof one);
}

void ChangeServer::run()
{
    while (!m_stopped)
        runOnce(1000);
}

void ChangeServer::runOnce(int timeoutMs)
{
    std::vector<pollfd> fds;
    std::vector<Client *> polled;
    fds.push_back({m_wake, POLLIN, 0});
    fds.push_back({m_listen, POLLIN, 0});
    fds.push_back({m_source.fd(), POLLIN, 0});
    for (Client &client : m_clients) {
        short events = POLLIN;
        if (client.outSent < client.out.size())
            events |= POLLOUT;
        fds.push_back({client.fd, events, 0});
        polled.push_back(&client);
    }
    if (::poll(fds.data(), fds.size(), timeoutMs) < 0)
        return;

    if (fds[0].revents & POLLIN) {
        std::uint64_t count;
        [[maybe_unused]] const ssize_t got = ::read(m_wake, &count, sizeof count);
    }
    for (std::size_t i = 0; i < polled.size(); ++i) {
        Client &client = *polled[i];
        const short revents = fds[3 + i].revents;
        if (revents & POLLOUT)
            flushOut(client);
        if (revents & (POLLIN | POLLHUP | POLLERR))
            readFrom(client);
    }
    if (fds[2].revents & POLLIN) {
        std::vector<ChangeEvent> events;
        m_source.read(events);
        deliver(events);
    }
    if (fds[1].revents & POLLIN)
        accept();
    for (Client &client : m_clients)
        if (!client.dead && client.outSent < client.out.size())
            flushOut(client);
    reap();
}

void ChangeServer::accept()
{
    for (int i = 0; i < kAcceptsPerRound; ++i) {
        const int fd = ::accept4(m_listen, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0)
            return;
        Credentials who;
        if (!m_credentialsOf(fd, who) || m_clients.size() >= m_limits.clients) {
            ::close(fd);
            continue;
        }
        std::size_t sameUser = 0;
        for (const Client &client : m_clients)
            sameUser += client.who.uid == who.uid;
        if (sameUser >= m_limits.clientsPerUser) {
            ::close(fd);
            continue;
        }
        Client client;
        client.fd = fd;
        client.who = std::move(who);
        m_clients.push_back(std::move(client));
        m_clientCount = m_clients.size();
    }
}

void ChangeServer::readFrom(Client &client)
{
    char buffer[8192];
    std::size_t taken = 0;
    while (!client.dead && taken < kReadPerRound) {
        const ssize_t size = ::recv(client.fd, buffer, sizeof buffer, MSG_DONTWAIT);
        if (size == 0) {
            client.dead = true;
            return;
        }
        if (size < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                client.dead = true;
            return;
        }
        taken += std::size_t(size);
        std::string_view chunk(buffer, std::size_t(size));
        while (!chunk.empty() && !client.dead) {
            const std::size_t newline = chunk.find('\n');
            if (client.discarding) {
                if (newline == std::string_view::npos)
                    break;
                client.discarding = false;
                send(client, "error too-long");
                chunk.remove_prefix(newline + 1);
                continue;
            }
            const std::string_view piece = chunk.substr(0, newline);
            if (client.in.size() + piece.size() + 1 > kMaxMessage) {
                // Longer than any message may be: thrown away up to its end.
                client.in.clear();
                if (newline == std::string_view::npos) {
                    client.discarding = true;
                    break;
                }
                send(client, "error too-long");
                chunk.remove_prefix(newline + 1);
                continue;
            }
            client.in.append(piece);
            if (newline == std::string_view::npos)
                break;
            const std::string line = std::move(client.in);
            client.in.clear();
            handleLine(client, line);
            chunk.remove_prefix(newline + 1);
        }
    }
}

void ChangeServer::handleLine(Client &client, std::string_view line)
{
    const auto words = splitWords(line);
    if (!words) {
        send(client, "error malformed");
        return;
    }
    const std::vector<std::string_view> &w = *words;
    const std::string_view command = w[0];
    if (command == "ping") {
        send(client, w.size() == 1 ? "pong" : "error malformed");
    } else if (command == "subscribe" || command == "unsubscribe") {
        if (w.size() != 2)
            send(client, "error malformed");
        else if (command == "subscribe")
            subscribe(client, w[1]);
        else
            unsubscribe(client, w[1]);
    } else {
        send(client, "error unknown-command");
    }
}

void ChangeServer::subscribe(Client &client, std::string_view text)
{
    auto asked = decodePath(text);
    while (asked && asked->size() > 1 && asked->back() == '/')
        asked->pop_back();
    // Whole and clean paths only: events are named below the path as it was given.
    if (!asked || (*asked)[0] != '/' || asked->size() >= PATH_MAX || asked->find("//") != std::string::npos
        || asked->find("/./") != std::string::npos || asked->find("/../") != std::string::npos
        || asked->ends_with("/.") || asked->ends_with("/..")) {
        send(client, "error malformed");
        return;
    }
    const std::string encoded = encodePath(*asked);
    for (const Subscription &sub : client.subscriptions)
        if (sub.asked == *asked) {
            send(client, "ok subscribe " + encoded);
            return;
        }
    if (client.subscriptions.size() >= kMaxSubscriptions) {
        send(client, "error too-many " + encoded);
        return;
    }
    // One answer for every refusal, so asking tells nothing about folders the client cannot see.
    char *real = ::realpath(asked->c_str(), nullptr);
    if (!real) {
        send(client, "error not-readable " + encoded);
        return;
    }
    std::string resolved(real);
    std::free(real);
    if (!mayWatch(resolved, client.who)) {
        send(client, "error not-readable " + encoded);
        return;
    }
    std::string why;
    if (!m_source.watch(resolved, why)) {
        send(client, "error cannot-watch " + encoded);
        return;
    }
    client.subscriptions.push_back({*asked, std::move(resolved)});
    send(client, "ok subscribe " + encoded);
}

void ChangeServer::unsubscribe(Client &client, std::string_view text)
{
    const auto asked = decodePath(text);
    if (!asked) {
        send(client, "error malformed");
        return;
    }
    const std::string encoded = encodePath(*asked);
    for (auto it = client.subscriptions.begin(); it != client.subscriptions.end(); ++it)
        if (it->asked == *asked) {
            client.subscriptions.erase(it);
            send(client, "ok unsubscribe " + encoded);
            return;
        }
    send(client, "error not-subscribed " + encoded);
}

void ChangeServer::send(Client &client, const std::string &line)
{
    if (client.dead)
        return;
    if (client.out.size() - client.outSent + line.size() + 1 > m_limits.queueBytes) {
        flushOut(client);
        if (client.dead)
            return;
        if (client.out.size() - client.outSent + line.size() + 1 > m_limits.queueBytes) {
            // It stopped reading: dropped, so it can hold neither memory nor the others back.
            client.dead = true;
            ++m_dropped;
            return;
        }
    }
    client.out.append(line);
    client.out.push_back('\n');
}

void ChangeServer::flushOut(Client &client)
{
    while (!client.dead && client.outSent < client.out.size()) {
        const ssize_t sent = ::send(client.fd, client.out.data() + client.outSent, client.out.size() - client.outSent,
                                    MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent > 0) {
            client.outSent += std::size_t(sent);
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            break;
        client.dead = true;
    }
    if (client.outSent == client.out.size()) {
        client.out.clear();
        client.outSent = 0;
    } else if (client.outSent > 64 * 1024) {
        client.out.erase(0, client.outSent);
        client.outSent = 0;
    }
}

void ChangeServer::deliver(const std::vector<ChangeEvent> &events)
{
    // What each client may see of a folder, worked out once per batch: the check is made when the
    // events are handed on, not when the client subscribed.
    std::map<std::pair<const Client *, std::string>, bool> seen;
    for (const ChangeEvent &event : events) {
        if (event.overflow) {
            for (Client &client : m_clients)
                if (!client.subscriptions.empty())
                    send(client, "overflow");
            continue;
        }
        for (Client &client : m_clients) {
            if (client.dead)
                continue;
            const Subscription *match = nullptr;
            for (const Subscription &sub : client.subscriptions)
                if (within(event.path, sub.resolved)) {
                    match = &sub;
                    break;
                }
            if (!match)
                continue;
            const auto key = std::make_pair(static_cast<const Client *>(&client), parentOf(event.path));
            auto known = seen.find(key);
            if (known == seen.end())
                known = seen.emplace(key, mayReveal(event.path, client.who)).first;
            if (!known->second)
                continue;
            ChangeEvent shown = event;
            const std::string rest = match->resolved == "/" ? event.path : event.path.substr(match->resolved.size());
            shown.path = match->asked == "/" ? rest : match->asked + rest;
            if (shown.path.empty())
                shown.path = match->asked;
            const std::string line = changeLine(shown);
            send(client, line.empty() ? std::string("overflow") : line);
        }
    }
}

void ChangeServer::closeClient(Client &client)
{
    if (client.fd >= 0)
        ::close(client.fd);
    client.fd = -1;
}

void ChangeServer::reap()
{
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (it->dead) {
            closeClient(*it);
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
    m_clientCount = m_clients.size();
}

} // namespace Riley
