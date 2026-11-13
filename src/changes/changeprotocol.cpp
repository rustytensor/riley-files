// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "changeprotocol.h"

namespace Riley {

namespace {

constexpr char kHex[] = "0123456789ABCDEF";

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool mustEscape(unsigned char c)
{
    return c <= 0x20 || c == 0x7f || c == '%';
}

} // namespace

std::string_view kindName(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Created:
        return "created";
    case ChangeKind::Deleted:
        return "deleted";
    case ChangeKind::MovedFrom:
        return "moved-from";
    case ChangeKind::MovedTo:
        return "moved-to";
    case ChangeKind::Written:
        return "written";
    }
    return "written";
}

std::optional<ChangeKind> kindFromName(std::string_view name)
{
    for (ChangeKind kind : {ChangeKind::Created, ChangeKind::Deleted, ChangeKind::MovedFrom, ChangeKind::MovedTo,
                            ChangeKind::Written})
        if (kindName(kind) == name)
            return kind;
    return std::nullopt;
}

std::string encodePath(std::string_view path)
{
    std::string out;
    out.reserve(path.size());
    for (char ch : path) {
        const auto c = static_cast<unsigned char>(ch);
        if (mustEscape(c)) {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0xf]);
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::optional<std::string> decodePath(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '%') {
            if (i + 2 >= text.size())
                return std::nullopt;
            const int hi = hexValue(text[i + 1]);
            const int lo = hexValue(text[i + 2]);
            if (hi < 0 || lo < 0)
                return std::nullopt;
            const char decoded = static_cast<char>(hi * 16 + lo);
            if (decoded == '\0')
                return std::nullopt;
            out.push_back(decoded);
            i += 2;
        } else if (mustEscape(c)) {
            return std::nullopt;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    if (out.empty())
        return std::nullopt;
    return out;
}

std::optional<std::vector<std::string_view>> splitWords(std::string_view line)
{
    std::vector<std::string_view> words;
    std::size_t start = 0;
    while (true) {
        const std::size_t space = line.find(' ', start);
        const std::string_view word = line.substr(start, space == std::string_view::npos ? std::string_view::npos
                                                                                          : space - start);
        if (word.empty())
            return std::nullopt;
        words.push_back(word);
        if (space == std::string_view::npos)
            break;
        start = space + 1;
    }
    return words;
}

std::string changeLine(const ChangeEvent &event)
{
    if (event.overflow)
        return "overflow";
    std::string line = "change ";
    line += kindName(event.kind);
    line += event.folder ? " folder " : " file ";
    line += encodePath(event.path);
    if (line.size() + 1 > kMaxMessage)
        return {};
    return line;
}

HelperMessage parseHelperLine(std::string_view line)
{
    HelperMessage message;
    const auto words = splitWords(line);
    if (!words || words->empty())
        return message;
    const std::vector<std::string_view> &w = *words;
    auto pathAt = [&](std::size_t i) -> std::optional<std::string> {
        return i < w.size() ? decodePath(w[i]) : std::nullopt;
    };
    if (w[0] == "pong" && w.size() == 1) {
        message.type = HelperMessage::Pong;
    } else if (w[0] == "overflow" && w.size() == 1) {
        message.type = HelperMessage::Overflow;
        message.event.overflow = true;
    } else if (w[0] == "ok" && w.size() == 3) {
        if (auto path = pathAt(2)) {
            message.type = HelperMessage::Ok;
            message.what = std::string(w[1]);
            message.path = *path;
        }
    } else if (w[0] == "error" && (w.size() == 2 || w.size() == 3)) {
        message.type = HelperMessage::Error;
        message.what = std::string(w[1]);
        if (w.size() == 3)
            message.path = pathAt(2).value_or(std::string());
    } else if (w[0] == "change" && w.size() == 4) {
        const auto kind = kindFromName(w[1]);
        const auto path = pathAt(3);
        if (kind && path && (w[2] == "file" || w[2] == "folder")) {
            message.type = HelperMessage::Change;
            message.path = *path;
            message.event.kind = *kind;
            message.event.folder = w[2] == "folder";
            message.event.path = *path;
        }
    }
    return message;
}

} // namespace Riley
