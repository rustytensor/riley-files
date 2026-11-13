// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "fanotifysource.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/fanotify.h>
#include <sys/statfs.h>
#include <unistd.h>

namespace Riley {

namespace {

constexpr std::uint64_t kMask = FAN_CREATE | FAN_DELETE | FAN_MOVED_FROM | FAN_MOVED_TO | FAN_CLOSE_WRITE | FAN_ONDIR;
constexpr std::size_t kFoldersPerFilesystem = 64;
constexpr int kReadsPerCall = 8;

// The path of an open descriptor, from /proc. Empty when the folder is gone.
std::string pathOf(int fd)
{
    char link[64];
    std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    char target[PATH_MAX + 1];
    const ssize_t size = ::readlink(link, target, PATH_MAX);
    if (size <= 0 || target[0] != '/')
        return {};
    std::string path(target, std::size_t(size));
    static const std::string deleted = " (deleted)";
    if (path.size() > deleted.size() && path.compare(path.size() - deleted.size(), deleted.size(), deleted) == 0)
        return {};
    return path;
}

} // namespace

FanotifySource::FanotifySource()
{
    m_buffer.resize(64 * 1024 / sizeof(std::uint64_t));
    m_fd = ::fanotify_init(FAN_CLASS_NOTIF | FAN_REPORT_DFID_NAME | FAN_CLOEXEC | FAN_NONBLOCK,
                           O_RDONLY | O_LARGEFILE | O_CLOEXEC);
    if (m_fd < 0)
        m_error = std::string("fanotify: ") + std::strerror(errno);
}

FanotifySource::~FanotifySource()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool FanotifySource::isValid(std::string *error) const
{
    if (error)
        *error = m_error;
    return m_fd >= 0;
}

bool FanotifySource::watch(const std::string &folder, std::string &error)
{
    if (m_fd < 0) {
        error = m_error;
        return false;
    }
    struct statfs fs;
    if (::statfs(folder.c_str(), &fs) != 0) {
        error = std::strerror(errno);
        return false;
    }
    int raw[2];
    static_assert(sizeof raw == sizeof fs.f_fsid);
    std::memcpy(raw, &fs.f_fsid, sizeof raw);
    const Fsid fsid{raw[0], raw[1]};
    auto found = m_marked.find(fsid);
    if (found == m_marked.end()) {
        if (::fanotify_mark(m_fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM, kMask, AT_FDCWD, folder.c_str()) != 0) {
            error = std::string("fanotify_mark: ") + std::strerror(errno);
            return false;
        }
        found = m_marked.emplace(fsid, std::vector<std::string>{}).first;
    }
    std::vector<std::string> &folders = found->second;
    if (std::find(folders.begin(), folders.end(), folder) == folders.end() && folders.size() < kFoldersPerFilesystem)
        folders.push_back(folder);
    return true;
}

void FanotifySource::read(std::vector<ChangeEvent> &out)
{
    if (m_fd < 0)
        return;
    auto *buffer = reinterpret_cast<unsigned char *>(m_buffer.data());
    const std::size_t bufferSize = m_buffer.size() * sizeof(std::uint64_t);
    std::map<Fsid, int> mountFds; // opened for this call only
    std::map<std::string, std::string> folders; // fsid + handle -> folder path, for this call

    auto mountFd = [&](const Fsid &fsid) {
        auto open = mountFds.find(fsid);
        if (open != mountFds.end())
            return open->second;
        int fd = -1;
        const auto marked = m_marked.find(fsid);
        if (marked != m_marked.end())
            for (const std::string &folder : marked->second)
                if ((fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) >= 0)
                    break;
        mountFds.emplace(fsid, fd);
        return fd;
    };

    for (int round = 0; round < kReadsPerCall; ++round) {
        const ssize_t size = ::read(m_fd, buffer, bufferSize);
        if (size <= 0)
            break;
        auto *meta = reinterpret_cast<fanotify_event_metadata *>(buffer);
        ssize_t left = size;
        for (; FAN_EVENT_OK(meta, left); meta = FAN_EVENT_NEXT(meta, left)) {
            if (meta->fd >= 0)
                ::close(meta->fd);
            if (meta->vers != FANOTIFY_METADATA_VERSION)
                continue;
            if (meta->mask & FAN_Q_OVERFLOW) {
                ChangeEvent event;
                event.overflow = true;
                out.push_back(event);
                continue;
            }
            // The info records after the metadata: the one we asked for holds the folder's
            // handle and the name.
            const auto *at = reinterpret_cast<const unsigned char *>(meta) + meta->metadata_len;
            const auto *end = reinterpret_cast<const unsigned char *>(meta) + meta->event_len;
            std::string path;
            while (at + sizeof(fanotify_event_info_header) <= end) {
                fanotify_event_info_header header;
                std::memcpy(&header, at, sizeof header);
                if (header.len < sizeof header || at + header.len > end)
                    break;
                const bool withName = header.info_type == FAN_EVENT_INFO_TYPE_DFID_NAME;
                if ((withName || header.info_type == FAN_EVENT_INFO_TYPE_DFID)
                    && header.len >= sizeof(fanotify_event_info_fid) + sizeof(file_handle)) {
                    const auto *info = reinterpret_cast<const fanotify_event_info_fid *>(at);
                    int raw[2];
                    std::memcpy(raw, &info->fsid, sizeof raw);
                    const Fsid fsid{raw[0], raw[1]};
                    const auto *handleStart = at + sizeof(fanotify_event_info_fid);
                    file_handle handleHead;
                    std::memcpy(&handleHead, handleStart, sizeof handleHead);
                    const auto *handleEnd = handleStart + sizeof(file_handle) + handleHead.handle_bytes;
                    if (handleHead.handle_bytes > MAX_HANDLE_SZ || handleEnd > at + header.len)
                        break;
                    std::string name;
                    if (withName) {
                        const auto *nameEnd = static_cast<const unsigned char *>(
                            std::memchr(handleEnd, '\0', std::size_t(at + header.len - handleEnd)));
                        if (!nameEnd)
                            break;
                        name.assign(reinterpret_cast<const char *>(handleEnd), std::size_t(nameEnd - handleEnd));
                        if (name.find('/') != std::string::npos || name == "..")
                            break;
                    }
                    std::string key(reinterpret_cast<const char *>(raw), sizeof raw);
                    key.append(reinterpret_cast<const char *>(handleStart), std::size_t(handleEnd - handleStart));
                    auto known = folders.find(key);
                    if (known == folders.end()) {
                        std::string folder;
                        const int mfd = mountFd(fsid);
                        if (mfd >= 0) {
                            alignas(file_handle) unsigned char copy[sizeof(file_handle) + MAX_HANDLE_SZ];
                            std::memcpy(copy, handleStart, std::size_t(handleEnd - handleStart));
                            const int dir = ::open_by_handle_at(mfd, reinterpret_cast<file_handle *>(copy),
                                                                O_PATH | O_CLOEXEC);
                            if (dir >= 0) {
                                folder = pathOf(dir);
                                ::close(dir);
                            }
                        }
                        known = folders.emplace(std::move(key), std::move(folder)).first;
                    }
                    if (!known->second.empty()) {
                        if (name.empty() || name == ".")
                            path = known->second;
                        else
                            path = (known->second == "/" ? std::string() : known->second) + '/' + name;
                    }
                    break;
                }
                at += header.len;
            }
            if (path.empty())
                continue; // the folder is gone already, or the record was not one we can use

            const bool folder = meta->mask & FAN_ONDIR;
            const std::pair<std::uint64_t, ChangeKind> kinds[] = {
                {FAN_CREATE, ChangeKind::Created},     {FAN_MOVED_FROM, ChangeKind::MovedFrom},
                {FAN_MOVED_TO, ChangeKind::MovedTo},   {FAN_CLOSE_WRITE, ChangeKind::Written},
                {FAN_DELETE, ChangeKind::Deleted},
            };
            for (const auto &[bit, kind] : kinds) {
                if (!(meta->mask & bit))
                    continue;
                ChangeEvent event;
                event.kind = kind;
                event.path = path;
                event.folder = folder;
                out.push_back(event);
            }
        }
        if (size < ssize_t(bufferSize) / 2)
            break; // nothing more waiting, most likely; poll says when there is
    }
    for (const auto &[fsid, fd] : mountFds)
        if (fd >= 0)
            ::close(fd);
}

} // namespace Riley
