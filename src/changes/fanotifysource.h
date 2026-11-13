// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "eventsource.h"

#include <map>
#include <string>
#include <cstdint>
#include <utility>
#include <vector>

namespace Riley {

// The changes of whole file systems from fanotify: one FAN_MARK_FILESYSTEM mark per file system,
// never a watch per folder. Events come with the folder's file handle and the name
// (FAN_REPORT_DFID_NAME); the folder's path is found again with open_by_handle_at. Needs
// CAP_SYS_ADMIN (fanotify) and CAP_DAC_READ_SEARCH (open_by_handle_at).
//
// No descriptor is kept open on a watched file system between reads, so a drive can still be
// unmounted while it is watched.
class FanotifySource : public EventSource
{
public:
    FanotifySource();
    ~FanotifySource() override;

    // False, with the reason, when fanotify could not be started.
    bool isValid(std::string *error = nullptr) const;

    int fd() const override { return m_fd; }
    bool watch(const std::string &folder, std::string &error) override;
    void read(std::vector<ChangeEvent> &out) override;

private:
    using Fsid = std::pair<int, int>;

    int m_fd = -1;
    std::string m_error;
    // Each watched file system, with a folder on it to open when its handles are resolved.
    std::map<Fsid, std::vector<std::string>> m_marked;
    std::vector<std::uint64_t> m_buffer; // 64 KiB, aligned for the event records
};

} // namespace Riley
