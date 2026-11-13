// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "changeprotocol.h"

#include <string>
#include <vector>

namespace Riley {

// A client of the change helper (changeprotocol.h): connects to its socket, subscribes to
// folders, and reads what the helper sends. Non-blocking after connecting: poll fd() for reading
// and call receive().
class ChangeClient
{
public:
    // Where the helper listens: /run/riley-files/changes.sock, or RILEY_CHANGES_SOCKET when set.
    static std::string defaultSocket();

    ChangeClient() = default;
    ~ChangeClient();
    ChangeClient(const ChangeClient &) = delete;
    ChangeClient &operator=(const ChangeClient &) = delete;

    bool connectTo(const std::string &socketPath, std::string *error = nullptr);
    bool isConnected() const { return m_fd >= 0; }
    void close();
    int fd() const { return m_fd; }

    bool subscribe(const std::string &folder);
    bool unsubscribe(const std::string &folder);
    bool ping();
    // Sends one raw line (a newline is added). For tests of the helper.
    bool sendLine(const std::string &line);

    // Appends what has arrived. False when the helper closed the connection (then not connected).
    bool receive(std::vector<HelperMessage> &out);

private:
    int m_fd = -1;
    std::string m_in;
};

} // namespace Riley
