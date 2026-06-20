#include "MediaPath.hpp"
#include <algorithm>
#include <array>
#include <cctype>

namespace pld::mediapath {

static size_t lastSep(const std::string& p) {
    size_t a = p.find_last_of('/');
    size_t b = p.find_last_of('\\');
    if (a == std::string::npos) return b;
    if (b == std::string::npos) return a;
    return std::max(a, b);
}

std::string extensionLower(const std::string& path) {
    size_t sep = lastSep(path);
    std::string base = (sep == std::string::npos) ? path : path.substr(sep + 1);
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= base.size()) return "";
    std::string ext = base.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool isMediaFile(const std::string& path) {
    static const std::array<const char*, 19> kExts = {
        "mp4", "mov", "mkv", "avi", "webm", "m4v", "mpg", "mpeg", "ts", "flv", "wmv",
        "mp3", "m4a", "aac", "wav", "flac", "ogg", "opus", "3gp"};
    std::string ext = extensionLower(path);
    if (ext.empty()) return false;
    return std::any_of(kExts.begin(), kExts.end(),
                       [&](const char* e) { return ext == e; });
}

std::string fileStem(const std::string& path) {
    size_t sep = lastSep(path);
    std::string base = (sep == std::string::npos) ? path : path.substr(sep + 1);
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return base;
    return base.substr(0, dot);
}

} // namespace pld::mediapath
