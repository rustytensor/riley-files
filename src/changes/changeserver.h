// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "changeprotocol.h"
#include "eventsource.h"
#include "permissions.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <list>
#include <string>

namespace Riley {

// The change helper's server: the socket, its clients, and the events handed on to them.
//
// It is one thread polling everything; nothing it does blocks on a client. Writes are
// non-blocking, each client has a bounded queue of what it was not yet able to take, and a client
// whose queue fills (it stopped reading) is dropped. A line longer than kMaxMessage is thrown
// away with an error and the client goes on. A client may only subscribe to folders it can read,
// and an event goes only to the subscribers whose folder holds it and who could see it themselves
// at the moment it is handed on (permissions.h). Who a client is comes from the socket only.
//
// It never writes a file except its socket and never runs a program.
class ChangeServer
{
public:
    struct Limits
    {
        std::size_t clients = 256;
        std::size_t clientsPerUser = 32;
        std::size_t queueBytes = 256 * 1024; // per client
    };
    // How a client is known. By default SO_PEERCRED and SO_PEERGROUPS on its socket.
    using CredentialsOf = std::function<bool(int socket, Credentials &out)>;

    ChangeServer(EventSource &source, std::string socketPath);
    ChangeServer(EventSource &source, std::string socketPath, Limits limits, CredentialsOf credentialsOf = {});
    ~ChangeServer();

    // Makes the socket (mode 0666: any user may connect; what each gets is filtered). An old
    // socket left at the path is replaced; anything else there is an error.
    bool listen(std::string &error);
    // Waits up to timeoutMs for something to do, does it, and returns.
    void runOnce(int timeoutMs);
    // Runs until stop().
    void run();
    // From any thread, or a signal handler.
    void stop();

    std::size_t clientCount() const { return m_clientCount.load(); }
    // Clients dropped because they stopped reading, since the start.
    std::size_t droppedCount() const { return m_dropped.load(); }

    static bool peerCredentials(int socket, Credentials &out);

private:
    struct Subscription
    {
        std::string asked;    // the path the client sent
        std::string resolved; // the same folder without links, as events name it
    };
    struct Client
    {
        int fd = -1;
        Credentials who;
        std::string in;
        bool discarding = false; // inside a line that was too long
        std::string out;         // what it has not yet taken
        std::size_t outSent = 0;
        std::list<Subscription> subscriptions;
        bool dead = false;
    };

    void accept();
    void readFrom(Client &client);
    void handleLine(Client &client, std::string_view line);
    void subscribe(Client &client, std::string_view text);
    void unsubscribe(Client &client, std::string_view text);
    void send(Client &client, const std::string &line);
    void flushOut(Client &client);
    void deliver(const std::vector<ChangeEvent> &events);
    void closeClient(Client &client);
    void reap();

    EventSource &m_source;
    std::string m_socketPath;
    Limits m_limits;
    CredentialsOf m_credentialsOf;
    int m_listen = -1;
    int m_wake = -1; // an eventfd for stop()
    std::atomic<bool> m_stopped = false;
    std::list<Client> m_clients;
    std::atomic<std::size_t> m_clientCount = 0;
    std::atomic<std::size_t> m_dropped = 0;
};

} // namespace Riley
