#include "Format.hpp"
#include <cstdio>

namespace pld {

std::string formatDuration(long long ms) {
    if (ms < 0) return "";
    long long totalSec = ms / 1000;
    long long h = totalSec / 3600;
    long long m = (totalSec % 3600) / 60;
    long long s = totalSec % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return std::string(buf);
}

} // namespace pld
