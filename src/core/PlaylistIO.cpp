#include "PlaylistIO.hpp"
#include "MediaPath.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

using nlohmann::json;

namespace pld::io {

std::string toJson(const std::string& name, const std::vector<PlaylistItem>& items) {
    json j;
    j["version"] = 1;
    j["name"] = name;
    j["items"] = json::array();
    for (const auto& it : items)
        j["items"].push_back({{"path", it.path}, {"title", it.title}});
    return j.dump(2);
}

bool fromJson(const std::string& text, std::string& nameOut, std::vector<PlaylistItem>& itemsOut) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    if (!j.contains("items") || !j["items"].is_array()) return false;
    nameOut = j.value("name", std::string{});
    itemsOut.clear();
    for (const auto& e : j["items"]) {
        if (!e.is_object() || !e.contains("path") || !e["path"].is_string()) return false;
        PlaylistItem it;
        it.path = e["path"].get<std::string>();
        it.title = e.value("title", std::string{});
        if (it.title.empty()) it.title = mediapath::fileStem(it.path);
        itemsOut.push_back(std::move(it));
    }
    return true;
}

std::string toM3u(const std::vector<PlaylistItem>& items) {
    std::ostringstream os;
    os << "#EXTM3U\n";
    for (const auto& it : items) {
        os << "#EXTINF:-1," << it.title << "\n";
        os << it.path << "\n";
    }
    return os.str();
}

static std::string trimCR(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

std::vector<PlaylistItem> parseM3u(const std::string& text) {
    std::vector<PlaylistItem> items;
    std::istringstream is(text);
    std::string line;
    std::string pendingTitle;
    bool haveTitle = false;
    while (std::getline(is, line)) {
        line = trimCR(line);
        if (line.empty()) continue;
        if (line.rfind("#EXTINF:", 0) == 0) {
            size_t comma = line.find(',');
            pendingTitle = (comma == std::string::npos) ? "" : line.substr(comma + 1);
            haveTitle = true;
            continue;
        }
        if (line[0] == '#') continue; // comment / directive
        PlaylistItem it;
        it.path = line;
        it.title = (haveTitle && !pendingTitle.empty()) ? pendingTitle
                                                        : mediapath::fileStem(line);
        items.push_back(std::move(it));
        pendingTitle.clear();
        haveTitle = false;
    }
    return items;
}

} // namespace pld::io
