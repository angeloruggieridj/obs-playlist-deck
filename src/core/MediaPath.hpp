// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace pld::mediapath {

std::string extensionLower(const std::string& path);
bool isMediaFile(const std::string& path);
std::string fileStem(const std::string& path);

// File name with its extension ("C:/a/b/clip.mp4" -> "clip.mp4").
std::string fileName(const std::string& path);

// True for a path that already names a location on its own: a POSIX "/x", a
// Windows drive path "C:\x" or "C:/x", a UNC share "\host\share", or "~".
bool isAbsolute(const std::string& path);

// Resolves `path` against `baseDir` when it is relative, which is how a .m3u
// written next to its media (the portable layout every other player produces)
// is meant to be read. An absolute path, an empty baseDir or a URL is returned
// untouched.
std::string resolveAgainst(const std::string& baseDir, const std::string& path);

// Inverse of resolveAgainst for the common case: returns `path` relative to
// `baseDir` when it lives inside it, otherwise `path` unchanged. Used by the
// "portable playlist" save option.
std::string relativeTo(const std::string& baseDir, const std::string& path);

// True when the string looks like a URL (scheme://...), which the playlist
// carries through verbatim rather than treating as a file path.
bool isUrl(const std::string& path);

// Orders names the way a person reads them: "clip2" before "clip10". Compares
// digit runs numerically and everything else case-insensitively. Returns true
// when `a` sorts before `b`.
bool naturalLess(const std::string& a, const std::string& b);

} // namespace pld::mediapath
