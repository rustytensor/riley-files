// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "eventsource.h"

#include <cstdint>
#include <sys/eventfd.h>
#include <unistd.h>

namespace Riley {

TestEventSource::TestEventSource()
    : m_fd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
{
}

TestEventSource::~TestEventSource()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool TestEventSource::watch(const std::string &folder, std::string &)
{
    std::lock_guard lock(m_mutex);
    m_watched.push_back(folder);
    return true;
}

void TestEventSource::read(std::vector<ChangeEvent> &out)
{
    std::uint64_t count;
    while (::read(m_fd, &count, sizeof count) == sizeof count) {
    }
    std::lock_guard lock(m_mutex);
    // A bounded batch, as fanotify's reads are; the rest stays readable for the next round.
    for (int i = 0; i < 512 && !m_queue.empty(); ++i) {
        out.push_back(std::move(m_queue.front()));
        m_queue.pop_front();
    }
    if (!m_queue.empty()) {
        const std::uint64_t one = 1;
        [[maybe_unused]] const ssize_t written = ::write(m_fd, &one, sizeof one);
    }
}

void TestEventSource::push(const ChangeEvent &event)
{
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(event);
    }
    const std::uint64_t one = 1;
    [[maybe_unused]] const ssize_t written = ::write(m_fd, &one, sizeof one);
}

void TestEventSource::push(ChangeKind kind, const std::string &path, bool folder)
{
    ChangeEvent event;
    event.kind = kind;
    event.path = path;
    event.folder = folder;
    push(event);
}

std::vector<std::string> TestEventSource::watched() const
{
    std::lock_guard lock(m_mutex);
    return m_watched;
}

} // namespace Riley
