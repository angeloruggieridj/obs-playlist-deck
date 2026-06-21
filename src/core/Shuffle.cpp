#include "Shuffle.hpp"

namespace pld {

int randomIndex(int count, int current, std::mt19937& rng) {
    if (count <= 0) return -1;
    if (count == 1) return 0;
    std::uniform_int_distribution<int> dist(0, count - 1);
    int idx = dist(rng);
    if (idx == current) idx = (idx + 1) % count; // avoid immediate repeat
    return idx;
}

} // namespace pld
