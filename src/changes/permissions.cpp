// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "permissions.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace Riley {

namespace {

constexpr unsigned kRead = 4;
constexpr unsigned kSearch = 1;

// The layout of system.posix_acl_access as the kernel stores it (little-endian).
constexpr std::uint32_t kAclVersion = 2;
enum AclTag : std::uint16_t {
    UserObj = 0x01,
    User = 0x02,
    GroupObj = 0x04,
    Group = 0x08,
    Mask = 0x10,
    Other = 0x20,
};
struct AclEntry
{
    std::uint16_t tag;
    std::uint16_t perm;
    std::uint32_t id;
};

class Fd
{
public:
    explicit Fd(int fd = -1) : m_fd(fd) {}
    ~Fd()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd &operator=(Fd &&other) noexcept
    {
        std::swap(m_fd, other.m_fd);
        return *this;
    }
    int get() const { return m_fd; }

private:
    int m_fd;
};

// Reads the folder's access ACL. False when it has none (or it cannot be read: then the mode bits
// decide, which is what they mean without an ACL).
bool readAcl(int fd, std::vector<AclEntry> &entries)
{
    unsigned char buffer[4 + 8 * 64];
    const ssize_t size = ::fgetxattr(fd, "system.posix_acl_access", buffer, sizeof buffer);
    if (size < 4 || (size - 4) % 8 != 0)
        return false;
    std::uint32_t version;
    std::memcpy(&version, buffer, 4);
    if (le32toh(version) != kAclVersion)
        return false;
    for (ssize_t at = 4; at < size; at += 8) {
        AclEntry entry;
        std::memcpy(&entry.tag, buffer + at, 2);
        std::memcpy(&entry.perm, buffer + at + 2, 2);
        std::memcpy(&entry.id, buffer + at + 4, 4);
        entry.tag = le16toh(entry.tag);
        entry.perm = le16toh(entry.perm);
        entry.id = le32toh(entry.id);
        entries.push_back(entry);
    }
    return true;
}

// The kernel's check (fs/namei.c acl_permission_check and posix_acl_permission) for one folder.
bool allowed(int fd, const struct stat &st, const Credentials &who, unsigned want)
{
    if (who.uid == 0)
        return true; // root reads and searches every folder
    if (st.st_uid == who.uid)
        return ((st.st_mode >> 6) & want) == want;

    std::vector<AclEntry> acl;
    if (readAcl(fd, acl)) {
        unsigned mask = 7;
        for (const AclEntry &e : acl)
            if (e.tag == Mask)
                mask = e.perm;
        for (const AclEntry &e : acl)
            if (e.tag == User && e.id == who.uid)
                return (e.perm & mask & want) == want;
        bool groupMatched = false;
        for (const AclEntry &e : acl) {
            const bool match = (e.tag == GroupObj && who.inGroup(st.st_gid)) || (e.tag == Group && who.inGroup(e.id));
            if (!match)
                continue;
            groupMatched = true;
            if ((e.perm & mask & want) == want)
                return true;
        }
        if (groupMatched)
            return false;
        for (const AclEntry &e : acl)
            if (e.tag == Other)
                return (e.perm & want) == want;
        return false;
    }

    if (who.inGroup(st.st_gid))
        return ((st.st_mode >> 3) & want) == want;
    return (st.st_mode & want) == want;
}

// Walks `folder` from "/": every folder on the way searchable, and the last one also allowed
// `last`. A part that is a link, or not a folder, or missing, refuses.
bool walk(const std::string &folder, const Credentials &who, unsigned last)
{
    if (folder.empty() || folder[0] != '/')
        return false;
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (current.get() < 0)
        return false;
    std::size_t at = 1;
    while (true) {
        struct stat st;
        if (::fstat(current.get(), &st) != 0)
            return false;
        while (at < folder.size() && folder[at] == '/')
            ++at;
        const bool isLast = at >= folder.size();
        if (!allowed(current.get(), st, who, isLast ? (last | kSearch) : kSearch))
            return false;
        if (isLast)
            return true;
        std::size_t end = folder.find('/', at);
        if (end == std::string::npos)
            end = folder.size();
        const std::string part = folder.substr(at, end - at);
        if (part == "." || part == "..")
            return false; // paths are given whole and clean
        Fd next(::openat(current.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0)
            return false;
        current = std::move(next);
        at = end;
    }
}

} // namespace

bool Credentials::inGroup(gid_t group) const
{
    return group == gid || std::find(groups.begin(), groups.end(), group) != groups.end();
}

bool mayWatch(const std::string &folder, const Credentials &who)
{
    return walk(folder, who, kRead);
}

bool mayReveal(const std::string &path, const Credentials &who)
{
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= path.size())
        return false;
    const std::string name = path.substr(slash + 1);
    if (name == "." || name == "..")
        return false;
    return walk(slash == 0 ? std::string("/") : path.substr(0, slash), who, kRead);
}

} // namespace Riley
