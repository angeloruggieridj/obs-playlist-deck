// SPDX-License-Identifier: MIT
#include "Heal.hpp"
#include "MediaPath.hpp"
#include <algorithm>
#include <cctype>

namespace pld {

namespace {
std::string lowered(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
} // namespace

HealMatch findMoved(const std::string& missingPath, const std::vector<std::string>& candidates) {
    HealMatch out;
    const std::string wanted = lowered(mediapath::fileName(missingPath));
    if (wanted.empty()) return out;

    for (const auto& candidate : candidates) {
        if (candidate == missingPath) continue; // the path we already know is gone
        if (lowered(mediapath::fileName(candidate)) != wanted) continue;
        if (!out.path.empty() && out.path != candidate) {
            // Two files with the same name in different folders. Guessing which
            // one the show wants is exactly the mistake worth avoiding.
            out.ambiguous = true;
            out.path.clear();
            return out;
        }
        out.path = candidate;
    }
    return out;
}

} // namespace pld
