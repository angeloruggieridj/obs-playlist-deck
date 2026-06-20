#pragma once
#include <string>

namespace pld {

// Returns the media duration in milliseconds, or -1 if it can't be determined
// (file missing, unreadable, or FFmpeg support not compiled in).
long long probeDurationMs(const std::string& path);

} // namespace pld
