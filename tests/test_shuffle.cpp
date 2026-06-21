#include "doctest/doctest.h"
#include "Shuffle.hpp"
using namespace pld;

TEST_CASE("randomIndex edge cases") {
    std::mt19937 rng(123);
    CHECK(randomIndex(0, -1, rng) == -1);
    CHECK(randomIndex(1, -1, rng) == 0);
    CHECK(randomIndex(1, 0, rng) == 0);
}

TEST_CASE("randomIndex stays in range and avoids immediate repeat") {
    std::mt19937 rng(42);
    for (int i = 0; i < 500; ++i) {
        int cur = i % 5;
        int idx = randomIndex(5, cur, rng);
        CHECK(idx >= 0);
        CHECK(idx < 5);
        CHECK(idx != cur);
    }
}

TEST_CASE("randomIndex is deterministic for a given seed") {
    std::mt19937 a(7), b(7);
    for (int i = 0; i < 20; ++i)
        CHECK(randomIndex(9, -1, a) == randomIndex(9, -1, b));
}
