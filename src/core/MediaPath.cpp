// SPDX-License-Identifier: MIT
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


std::string fileName(const std::string& path) {
    size_t sep = lastSep(path);
    return (sep == std::string::npos) ? path : path.substr(sep + 1);
}

bool isUrl(const std::string& path) {
    const size_t at = path.find("://");
    if (at == std::string::npos || at == 0) return false;
    // A scheme is letters/digits/+-. only, so "C:/x" and "\\host\share" are not
    // mistaken for one.
    for (size_t i = 0; i < at; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') return false;
    }
    return true;
}

bool isAbsolute(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '~') return true;
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') return true; // UNC \\host\share
    // A single leading backslash is drive-relative on Windows; there is no
    // better base for it than the playlist's own folder, so it is left alone
    // rather than glued onto one.
    if (path[0] == '\\') return true;
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\'))
        return true; // C:\ or C:/
    return false;
}

std::string resolveAgainst(const std::string& baseDir, const std::string& path) {
    if (path.empty() || baseDir.empty()) return path;
    if (isAbsolute(path) || isUrl(path)) return path;
    std::string base = baseDir;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    if (base.empty()) return path;
    return base + "/" + path;
}

// One path character, with separators unified and case folded.
static char normSep(char c) {
    if (c == '\\') return '/';
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string relativeTo(const std::string& baseDir, const std::string& path) {
    if (baseDir.empty() || path.empty() || isUrl(path)) return path;
    std::string base = baseDir;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    if (base.empty() || path.size() <= base.size() + 1) return path;
    for (size_t i = 0; i < base.size(); ++i)
        if (normSep(base[i]) != normSep(path[i])) return path;
    const char sep = path[base.size()];
    if (sep != '/' && sep != '\\') return path;
    return path.substr(base.size() + 1);
}

bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            // Whole digit runs compare as numbers, leading zeros aside, which is
            // what puts "clip2" before "clip10".
            size_t ia = i, ib = j;
            while (ia < a.size() && std::isdigit(static_cast<unsigned char>(a[ia]))) ++ia;
            while (ib < b.size() && std::isdigit(static_cast<unsigned char>(b[ib]))) ++ib;
            size_t sa = i, sb = j;
            while (sa + 1 < ia && a[sa] == '0') ++sa;
            while (sb + 1 < ib && b[sb] == '0') ++sb;
            const size_t la = ia - sa, lb = ib - sb;
            if (la != lb) return la < lb;
            const int cmp = a.compare(sa, la, b, sb, lb);
            if (cmp != 0) return cmp < 0;
            i = ia;
            j = ib;
            continue;
        }
        const char la = static_cast<char>(std::tolower(ca));
        const char lb = static_cast<char>(std::tolower(cb));
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    if (i < a.size()) return false;
    if (j < b.size()) return true;
    return a < b; // equal apart from case: deterministic tiebreak
}

} // namespace pld::mediapath
