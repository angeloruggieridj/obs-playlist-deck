// SPDX-License-Identifier: MIT
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>

using nlohmann::json;

namespace pld::io {

namespace {

// Path as it should be written out: relative to the playlist's folder when the
// portable option is on and the file actually lives under it.
std::string outPath(const PlaylistItem& it, const std::string& baseDir, bool relativePaths) {
    return relativePaths ? mediapath::relativeTo(baseDir, it.path) : it.path;
}

// Durations are milliseconds as integers here, but playlists written by other
// tools carry floats. nlohmann's value<long long>() returns the default for a
// number_float, which silently dropped them; round instead.
long long readDuration(const json& e) {
    if (!e.contains("duration")) return -1;
    const json& d = e["duration"];
    if (d.is_number_integer() || d.is_number_unsigned()) return d.get<long long>();
    if (d.is_number_float()) return static_cast<long long>(std::llround(d.get<double>()));
    return -1;
}

} // namespace

std::string toJson(const std::string& name, const std::vector<PlaylistItem>& items,
                   const std::string& baseDir, bool relativePaths) {
    json j;
    j["version"] = 1;
    j["name"] = name;
    j["items"] = json::array();
    for (const auto& it : items)
        j["items"].push_back({{"path", outPath(it, baseDir, relativePaths)},
                              {"title", it.title},
                              {"duration", it.durationMs}});
    return j.dump(2);
}

ParseResult fromJson(const std::string& text, std::string& nameOut,
                     std::vector<PlaylistItem>& itemsOut, const std::string& baseDir) {
    ParseResult res;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return res;
    if (!j.contains("items") || !j["items"].is_array()) return res;
    nameOut = j.value("name", std::string{});
    itemsOut.clear();
    for (const auto& e : j["items"]) {
        // One broken entry costs that entry, not the playlist: a file with 200
        // good items and 1 bad one used to be rejected whole.
        if (!e.is_object() || !e.contains("path") || !e["path"].is_string() ||
            e["path"].get<std::string>().empty()) {
            ++res.skipped;
            continue;
        }
        PlaylistItem it;
        it.path = mediapath::resolveAgainst(baseDir, e["path"].get<std::string>());
        it.title = e.contains("title") && e["title"].is_string() ? e["title"].get<std::string>()
                                                                 : std::string{};
        if (it.title.empty()) it.title = mediapath::fileStem(it.path);
        it.durationMs = readDuration(e);
        itemsOut.push_back(std::move(it));
    }
    res.ok = true;
    return res;
}

std::string toM3u(const std::vector<PlaylistItem>& items, const std::string& baseDir,
                  bool relativePaths) {
    std::ostringstream os;
    os << "#EXTM3U\n";
    for (const auto& it : items) {
        long long secs = (it.durationMs >= 0) ? (it.durationMs / 1000) : -1;
        os << "#EXTINF:" << secs << "," << it.title << "\n";
        os << outPath(it, baseDir, relativePaths) << "\n";
    }
    return os.str();
}

std::string toCsv(const std::vector<PlaylistItem>& items) {
    auto quote = [](const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += '"'; // RFC 4180 doubles an embedded quote
            out += c;
        }
        out += '"';
        return out;
    };
    std::ostringstream os;
    os << "title,path,duration_ms\n";
    for (const auto& it : items)
        os << quote(it.title) << ',' << quote(it.path) << ',' << it.durationMs << '\n';
    return os.str();
}

static std::string trimCR(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

std::vector<PlaylistItem> parseM3u(const std::string& text, const std::string& baseDir) {
    std::vector<PlaylistItem> items;
    std::istringstream is(text);
    std::string line;
    std::string pendingTitle;
    long long pendingDurationMs = -1;
    bool haveTitle = false;
    while (std::getline(is, line)) {
        line = trimCR(line);
        if (line.empty()) continue;
        if (line.rfind("#EXTINF:", 0) == 0) {
            size_t comma = line.find(',');
            std::string secsStr = line.substr(8, (comma == std::string::npos) ? std::string::npos
                                                                               : comma - 8);
            try {
                long long secs = std::stoll(secsStr);
                pendingDurationMs = (secs >= 0) ? secs * 1000 : -1;
            } catch (...) {
                pendingDurationMs = -1;
            }
            pendingTitle = (comma == std::string::npos) ? "" : line.substr(comma + 1);
            haveTitle = true;
            continue;
        }
        if (line[0] == '#') continue; // comment / directive
        PlaylistItem it;
        // Players write .m3u files with paths relative to the file itself; read
        // as absolute, every one of those items came out "file not found".
        it.path = mediapath::resolveAgainst(baseDir, line);
        it.title = (haveTitle && !pendingTitle.empty()) ? pendingTitle
                                                        : mediapath::fileStem(it.path);
        it.durationMs = pendingDurationMs;
        items.push_back(std::move(it));
        pendingTitle.clear();
        pendingDurationMs = -1;
        haveTitle = false;
    }
    return items;
}

} // namespace pld::io
