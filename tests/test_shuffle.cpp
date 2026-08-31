#include "doctest/doctest.h"
#include "Shuffle.hpp"
#include <algorithm>
#include <vector>
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

// F-16: "shuffle" meant a uniform draw each step, which both repeated items
// long before the list was exhausted and — because a draw equal to the current
// item was nudged to (current+1) — gave the item after the current one about
// twice everyone else's probability. A bag deals every item once before any
// repeats, which is what a shuffled deck does.
TEST_CASE("ShuffleQueue plays every item once before repeating") {
    std::mt19937 rng(2024);
    ShuffleQueue q;
    const int n = 10;
    std::vector<int> seen;
    for (int i = 0; i < n; ++i) seen.push_back(q.next(n, -1, rng));
    std::sort(seen.begin(), seen.end());
    for (int i = 0; i < n; ++i) CHECK(seen[i] == i); // a permutation, no repeats
}

TEST_CASE("ShuffleQueue refills automatically and never opens on the current item") {
    std::mt19937 rng(7);
    ShuffleQueue q;
    const int n = 6;
    int current = -1;
    for (int step = 0; step < 200; ++step) {
        const int idx = q.next(n, current, rng);
        REQUIRE(idx >= 0);
        REQUIRE(idx < n);
        if (step > 0) CHECK(idx != current); // no immediate repeat across refills
        current = idx;
    }
}

TEST_CASE("ShuffleQueue is uniform enough to be called a shuffle") {
    std::mt19937 rng(99);
    ShuffleQueue q;
    const int n = 8;
    const int rounds = 500;
    std::vector<int> hits(n, 0);
    int current = -1;
    for (int i = 0; i < n * rounds; ++i) {
        current = q.next(n, current, rng);
        ++hits[current];
    }
    // Every item is dealt once per bag, so each count is within one bag of the
    // mean whatever the seed. The old randomIndex() failed this by construction.
    for (int h : hits) {
        CHECK(h >= rounds - 2);
        CHECK(h <= rounds + 2);
    }
}

TEST_CASE("ShuffleQueue edge cases") {
    std::mt19937 rng(1);
    ShuffleQueue q;
    CHECK(q.next(0, -1, rng) == -1);
    CHECK(q.next(1, 0, rng) == 0); // a single item can only repeat
    q.reset(4, -1, rng);
    CHECK(q.size() == 4);
    const int peeked = q.peek();
    CHECK(q.next(4, -1, rng) == peeked); // peek does not consume
    q.invalidate();
    CHECK(q.empty());
    CHECK(q.peek() == -1);
}

TEST_CASE("a changed item count rebuilds the bag rather than returning stale indices") {
    std::mt19937 rng(5);
    ShuffleQueue q;
    q.reset(20, -1, rng);
    for (int i = 0; i < 50; ++i) {
        const int idx = q.next(3, -1, rng); // playlist shrank to 3
        CHECK(idx >= 0);
        CHECK(idx < 3);
    }
}
