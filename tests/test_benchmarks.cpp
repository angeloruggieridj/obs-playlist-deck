// Canary benchmarks. They log timings and never fail: an assertion on wall
// clock would be flaky on shared CI runners, and a number nobody can read is
// worse than no number. What they are for is the regression that turns a linear
// path quadratic — the kind that looks fine on a 20-item playlist and freezes a
// 2000-item one, which is exactly how the per-rebuild stat() bug shipped.
//
// Run them with:  playlist-deck-tests -ts="benchmarks" -s
#include "doctest/doctest.h"

#include "Playlist.hpp"
#include "PlaylistIO.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace {
std::vector<pld::PlaylistItem> makeItems(int count) {
    std::vector<pld::PlaylistItem> items;
    items.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string n = std::to_string(i);
        items.push_back({"/media/set/clip " + n + ".mp4", "Clip " + n, (i % 7) * 1000LL});
    }
    return items;
}

template <typename Fn>
double millis(Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}
} // namespace

TEST_SUITE("benchmarks") {

TEST_CASE("json round-trip of 10k items") {
    const auto items = makeItems(10000);
    std::string text;
    const double write = millis([&]() { text = pld::io::toJson("bench", items); });

    std::string name;
    std::vector<pld::PlaylistItem> back;
    const double read = millis([&]() { pld::io::fromJson(text, name, back); });

    MESSAGE("toJson 10k: " << write << " ms, fromJson 10k: " << read << " ms, "
                           << text.size() / 1024 << " KiB");
    CHECK(back.size() == items.size());
}

TEST_CASE("m3u round-trip of 10k items") {
    const auto items = makeItems(10000);
    std::string text;
    const double write = millis([&]() { text = pld::io::toM3u(items); });

    std::vector<pld::PlaylistItem> back;
    const double read = millis([&]() { back = pld::io::parseM3u(text, "/media/set"); });

    MESSAGE("toM3u 10k: " << write << " ms, parseM3u 10k: " << read << " ms");
    CHECK(back.size() == items.size());
}

TEST_CASE("playlist edits on 10k items") {
    pld::Playlist playlist;
    playlist.setItems(makeItems(10000));

    const double moves = millis([&]() {
        for (int i = 0; i < 1000; ++i) playlist.move(i, i + 1);
    });
    const double totals = millis([&]() {
        for (int i = 0; i < 100; ++i) (void)playlist.totalDurationMs();
    });
    // Removing from the front is the worst case for a vector, and the one a
    // "remove the first ten" click actually performs.
    const double removals = millis([&]() {
        for (int i = 0; i < 1000; ++i) playlist.removeAt(0);
    });

    MESSAGE("1000 moves: " << moves << " ms, 100 totals: " << totals
                           << " ms, 1000 removals: " << removals << " ms");
    CHECK(playlist.size() == 9000);
}

} // TEST_SUITE
