// SPDX-License-Identifier: MIT
#include "Shuffle.hpp"
#include <algorithm>

namespace pld {

int randomIndex(int count, int current, std::mt19937& rng) {
    if (count <= 0) return -1;
    if (count == 1) return 0;
    std::uniform_int_distribution<int> dist(0, count - 1);
    int idx = dist(rng);
    if (idx == current) idx = (idx + 1) % count; // avoid immediate repeat
    return idx;
}

void ShuffleQueue::reset(int count, int current, std::mt19937& rng) {
    bag_.clear();
    builtFor_ = count;
    if (count <= 0) return;
    bag_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) bag_.push_back(i);
    std::shuffle(bag_.begin(), bag_.end(), rng);
    // next() pops from the back, so the item playing right now must not sit
    // there. Swapping it with the front keeps the permutation uniform over the
    // remaining orderings and costs nothing.
    if (count > 1 && !bag_.empty() && bag_.back() == current)
        std::swap(bag_.front(), bag_.back());
}

int ShuffleQueue::next(int count, int current, std::mt19937& rng) {
    if (count <= 0) {
        invalidate();
        return -1;
    }
    // A changed item count invalidates the stored indices outright.
    if (bag_.empty() || builtFor_ != count) reset(count, current, rng);
    if (bag_.empty()) return -1;
    const int idx = bag_.back();
    bag_.pop_back();
    return idx;
}

} // namespace pld
