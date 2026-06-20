#pragma once
#include <string>

namespace pld {

// Formats a media duration in milliseconds as "m:ss" (or "h:mm:ss" when >= 1h).
// Returns "" when the duration is unknown (negative).
std::string formatDuration(long long ms);

} // namespace pld
