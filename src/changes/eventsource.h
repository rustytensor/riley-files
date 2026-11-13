// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "changeprotocol.h"

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Riley {

// Where the change helper's events come from: fanotify on the real machine (fanotifysource.h),
// or a test's own queue fed through the same server and protocol code (TestEventSource).
class EventSource
{
public:
    virtual ~EventSource() = default;
    // A descriptor that polls readable when events are waiting.
    virtual int fd() const = 0;
    // Starts watching the file system holding `folder` (once per file system). False with a
    // reason when it cannot be watched.
    virtual bool watch(const std::string &folder, std::string &error) = 0;
    // Appends the events waiting now, each with its full path.
    virtual void read(std::vector<ChangeEvent> &out) = 0;
};

// Events a test hands over with push(), from any thread.
class TestEventSource : public EventSource
{
public:
    TestEventSource();
    ~TestEventSource() override;

    int fd() const override { return m_fd; }
    bool watch(const std::string &folder, std::string &error) override;
    void read(std::vector<ChangeEvent> &out) override;

    void push(const ChangeEvent &event);
    void push(ChangeKind kind, const std::string &path, bool folder = false);
    std::vector<std::string> watched() const;

private:
    int m_fd = -1; // an eventfd
    mutable std::mutex m_mutex;
    std::deque<ChangeEvent> m_queue;
    std::vector<std::string> m_watched;
};

} // namespace Riley
