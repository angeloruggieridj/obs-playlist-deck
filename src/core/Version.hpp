#pragma once
#include <string>

namespace pld {

// Compares dotted version strings (an optional leading 'v' is ignored).
// Returns true when `latest` is strictly newer than `current`.
// Missing components are treated as 0 (e.g. "1.2" == "1.2.0").
bool isNewerVersion(const std::string& latest, const std::string& current);

} // namespace pld
