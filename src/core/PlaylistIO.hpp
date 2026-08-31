// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include "Playlist.hpp"

namespace pld::io {

// Outcome of reading a playlist file. `ok` is false only for a file that is not
// a playlist at all (unparseable, or no items array): a single malformed entry
// among good ones is skipped and counted, never a reason to throw away the rest
// of someone's playlist.
struct ParseResult {
    bool ok = false;
    size_t skipped = 0;
    explicit operator bool() const { return ok; }
};

// `baseDir`, when given, is the folder the playlist file lives in: relative
// paths inside the file are resolved against it, and with `relativePaths` a
// written playlist stores paths inside that folder relative to it, which is
// what makes a "gig folder" (playlist + media) portable between machines.
std::string toJson(const std::string& name, const std::vector<PlaylistItem>& items,
                   const std::string& baseDir = "", bool relativePaths = false);
ParseResult fromJson(const std::string& text, std::string& nameOut,
                     std::vector<PlaylistItem>& itemsOut, const std::string& baseDir = "");
std::string toM3u(const std::vector<PlaylistItem>& items, const std::string& baseDir = "",
                  bool relativePaths = false);
std::vector<PlaylistItem> parseM3u(const std::string& text, const std::string& baseDir = "");

// Comma-separated title,path,duration for reporting/handover. Export only —
// there is no CSV reader, on purpose: it is a report format, not a playlist.
std::string toCsv(const std::vector<PlaylistItem>& items);

} // namespace pld::io
