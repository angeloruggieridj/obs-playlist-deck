// SPDX-License-Identifier: MIT
#pragma once
#include <random>
#include <vector>

namespace pld {

// Returns a random index in [0, count) for shuffle playback. When count > 1 the
// result is guaranteed to differ from `current` (avoids immediate repeats).
// Returns -1 for an empty list. Deterministic for a given RNG state, so it is
// unit-testable with a seeded engine.
//
// Kept for the single-shot case; playback uses ShuffleQueue, which is what a DJ
// means by "shuffle" (see below).
int randomIndex(int count, int current, std::mt19937& rng);

// Bag shuffle: a Fisher-Yates permutation of every index is drawn once and
// played out before any index repeats, the way a shuffled deck is dealt.
//
// Why not randomIndex(): drawing uniformly each step both repeats items long
// before the list is exhausted and — because a draw equal to `current` was
// nudged to (current+1) — gave the item after the current one roughly twice the
// probability of every other. Neither is what "shuffle" promises.
class ShuffleQueue {
public:
    // Rebuilds the bag for a playlist of `count` items. `current`, when in
    // range, is kept off the front so the shuffle never opens by repeating the
    // item that is already playing.
    void reset(int count, int current, std::mt19937& rng);

    // Next index to play. Refills the bag (excluding `current`, as reset does)
    // once it runs out, so playback continues indefinitely. Returns -1 when the
    // playlist is empty.
    int next(int count, int current, std::mt19937& rng);

    // Index that next() would return, without consuming it (-1 when unknown).
    // Feeds the "up next" indicator.
    int peek() const { return bag_.empty() ? -1 : bag_.back(); }

    // Drops the bag: the next call to next() reshuffles. Called whenever the
    // playlist itself changes, since the stored indices no longer mean anything.
    void invalidate() { bag_.clear(); builtFor_ = -1; }

    bool empty() const { return bag_.empty(); }
    int size() const { return static_cast<int>(bag_.size()); }

private:
    // Indices still to play, in reverse order (back() is next).
    std::vector<int> bag_;
    int builtFor_ = -1; // item count the bag was built for
};

} // namespace pld
