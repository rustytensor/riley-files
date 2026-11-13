// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Riley {

// The protocol of the change helper (riley-files-changes), spoken on a Unix stream socket, by
// default /run/riley-files/changes.sock. Every message is one line of at most kMaxMessage bytes,
// the newline included. Paths are bytes; '%', every byte up to and including the space, and
// 0x7f are written as %XX (two upper-case hex digits), so a path is always one word.
//
// Client to helper:
//   subscribe <path>      changes below this folder, from now on (at most kMaxSubscriptions)
//   unsubscribe <path>
//   ping
// Helper to client:
//   ok subscribe <path> | ok unsubscribe <path>
//   error <reason> [<path>]   reason: too-long, malformed, unknown-command, not-readable,
//                             too-many, cannot-watch, not-subscribed
//   pong
//   change <created|deleted|moved-from|moved-to|written> <file|folder> <path>
//   overflow                  changes were lost: read the subscribed folders again
//
// The helper knows who a client is only from the socket (SO_PEERCRED), never from what it sends.
constexpr std::size_t kMaxMessage = 4096;
constexpr std::size_t kMaxSubscriptions = 64;

enum class ChangeKind { Created, Deleted, MovedFrom, MovedTo, Written };

// One change in a file system: what happened, and the full path it happened at.
struct ChangeEvent
{
    ChangeKind kind = ChangeKind::Created;
    std::string path;
    bool folder = false;
    bool overflow = false; // events were lost; path is empty
};

std::string_view kindName(ChangeKind kind);
std::optional<ChangeKind> kindFromName(std::string_view name);

std::string encodePath(std::string_view path);
// Empty when the text is not a well-formed encoded path (bad escapes, a raw byte that should
// have been escaped, a NUL).
std::optional<std::string> decodePath(std::string_view text);

// The words of a line, split at single spaces. Empty words (two spaces) make it malformed.
std::optional<std::vector<std::string_view>> splitWords(std::string_view line);

// "change ..." for an event, without the newline. Empty when it would not fit in one message.
std::string changeLine(const ChangeEvent &event);

// One line from the helper, read by a client.
struct HelperMessage
{
    enum Type { Ok, Error, Pong, Change, Overflow, Unknown } type = Unknown;
    std::string what;  // Ok: the command; Error: the reason
    std::string path;  // Ok, Error (when given), Change
    ChangeEvent event; // Change, Overflow
};

HelperMessage parseHelperLine(std::string_view line);

} // namespace Riley
