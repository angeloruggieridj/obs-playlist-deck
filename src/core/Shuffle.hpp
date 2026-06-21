#pragma once
#include <random>

namespace pld {

// Returns a random index in [0, count) for shuffle playback. When count > 1 the
// result is guaranteed to differ from `current` (avoids immediate repeats).
// Returns -1 for an empty list. Deterministic for a given RNG state, so it is
// unit-testable with a seeded engine.
int randomIndex(int count, int current, std::mt19937& rng);

} // namespace pld
