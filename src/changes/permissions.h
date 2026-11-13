// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace Riley {

// Who a client of the change helper is: taken from its socket (SO_PEERCRED and SO_PEERGROUPS),
// never from anything it sends.
struct Credentials
{
    uid_t uid = 0;
    gid_t gid = 0;
    std::vector<gid_t> groups; // supplementary groups
    pid_t pid = 0;

    bool inGroup(gid_t group) const;
};

// The permission filter. The helper runs as root and sees every change; it hands a change on only
// when the client could have seen it itself. Both checks walk the path from "/" one folder at a
// time without following links (a link in the path, or a part that cannot be opened, refuses),
// and apply the owner, group and other bits, and a POSIX access ACL when the folder has one,
// the way the kernel does for that uid, gid and groups.
//
// The client may watch `folder`: every folder above it searchable, and the folder itself
// readable and searchable.
bool mayWatch(const std::string &folder, const Credentials &who);
// The client may learn that `path` changed: every folder above it searchable and the folder
// holding it readable (so it could have listed the name). `path` itself need not exist.
bool mayReveal(const std::string &path, const Credentials &who);

} // namespace Riley
