#include "Version.hpp"
#include <sstream>
#include <vector>

namespace pld {

static std::vector<long> parseVersion(const std::string& s) {
    std::string v = s;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v = v.substr(1);
    std::vector<long> parts;
    std::stringstream ss(v);
    std::string tok;
    while (std::getline(ss, tok, '.')) {
        try {
            parts.push_back(std::stol(tok));
        } catch (...) {
            parts.push_back(0);
        }
    }
    return parts;
}

bool isNewerVersion(const std::string& latest, const std::string& current) {
    std::vector<long> a = parseVersion(latest);
    std::vector<long> b = parseVersion(current);
    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        long ai = (i < a.size()) ? a[i] : 0;
        long bi = (i < b.size()) ? b[i] : 0;
        if (ai != bi) return ai > bi;
    }
    return false;
}

} // namespace pld
