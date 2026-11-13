// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "changeclient.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Riley {

std::string ChangeClient::defaultSocket()
{
    if (const char *set = std::getenv("RILEY_CHANGES_SOCKET"); set && *set)
        return set;
    return "/run/riley-files/changes.sock";
}

ChangeClient::~ChangeClient()
{
    close();
}

void ChangeClient::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    m_in.clear();
}

bool ChangeClient::connectTo(const std::string &socketPath, std::string *error)
{
    close();
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socketPath.empty() || socketPath.size() >= sizeof address.sun_path) {
        if (error)
            *error = "socket path too long";
        return false;
    }
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
        if (error)
            *error = socketPath + ": " + std::strerror(errno);
        if (fd >= 0)
            ::close(fd);
        return false;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    m_fd = fd;
    return true;
}

bool ChangeClient::sendLine(const std::string &line)
{
    if (m_fd < 0)
        return false;
    std::string out = line + '\n';
    std::size_t sent = 0;
    while (sent < out.size()) {
        const ssize_t n = ::send(m_fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += std::size_t(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            pollfd p{m_fd, POLLOUT, 0};
            if (::poll(&p, 1, 1000) <= 0)
                return false;
        } else {
            return false;
        }
    }
    return true;
}

bool ChangeClient::subscribe(const std::string &folder)
{
    return sendLine("subscribe " + encodePath(folder));
}

bool ChangeClient::unsubscribe(const std::string &folder)
{
    return sendLine("unsubscribe " + encodePath(folder));
}

bool ChangeClient::ping()
{
    return sendLine("ping");
}

bool ChangeClient::receive(std::vector<HelperMessage> &out)
{
    if (m_fd < 0)
        return false;
    char buffer[16384];
    while (true) {
        const ssize_t size = ::recv(m_fd, buffer, sizeof buffer, MSG_DONTWAIT);
        if (size == 0) {
            close();
            return false;
        }
        if (size < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return true;
            close();
            return false;
        }
        m_in.append(buffer, std::size_t(size));
        std::size_t start = 0;
        while (true) {
            const std::size_t newline = m_in.find('\n', start);
            if (newline == std::string::npos)
                break;
            out.push_back(parseHelperLine(std::string_view(m_in).substr(start, newline - start)));
            start = newline + 1;
        }
        m_in.erase(0, start);
        if (m_in.size() > kMaxMessage) {
            close(); // the helper never sends that
            return false;
        }
    }
}

} // namespace Riley
