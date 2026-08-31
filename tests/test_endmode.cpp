#include "doctest/doctest.h"
#include "EndMode.hpp"
#include <string>
using namespace pld;

// F-11: the combo box used to map its row index straight onto the enum, so
// reordering the menu would have silently made "Stop" run a shuffle. The table
// is the mapping now, and it has to stay complete and unique.
TEST_CASE("the end-mode table covers every mode exactly once") {
    CHECK(kEndModeCount == 6);
    for (int v = 0; v < kEndModeCount; ++v) {
        int found = 0;
        for (const auto& m : kEndModes)
            if (static_cast<int>(m.mode) == v) ++found;
        CAPTURE(v);
        CHECK(found == 1);
    }
    for (const auto& m : kEndModes) {
        CAPTURE(m.key);
        CHECK(std::string(m.key).rfind("OnEnd.", 0) == 0);
        CHECK(std::string(m.tipKey).rfind("OnEndTip.", 0) == 0);
    }
}

TEST_CASE("an unknown persisted mode falls back to the default") {
    CHECK(endModeFromInt(0) == EndMode::PlayNext);
    CHECK(endModeFromInt(5) == EndMode::RepeatOne);
    CHECK(endModeFromInt(42) == EndMode::PlayNext);
    CHECK(endModeFromInt(-1) == EndMode::PlayNext);
    CHECK_FALSE(isValidEndMode(6));
}

TEST_CASE("decideOnEnd: play next stops at the end of the list") {
    auto d = decideOnEnd(EndMode::PlayNext, 3, 0, -1);
    CHECK(d.action == EndAction::Play);
    CHECK(d.index == 1);
    d = decideOnEnd(EndMode::PlayNext, 3, 2, -1);
    CHECK(d.action == EndAction::Stop);
}

TEST_CASE("decideOnEnd: loop wraps") {
    CHECK(decideOnEnd(EndMode::Loop, 3, 2, -1).index == 0);
    CHECK(decideOnEnd(EndMode::Loop, 3, 2, -1).action == EndAction::Play);
}

TEST_CASE("decideOnEnd: load-next stages the following item, and stops at the end") {
    auto d = decideOnEnd(EndMode::LoadNext, 3, 1, -1);
    CHECK(d.action == EndAction::StageNext);
    CHECK(d.index == 2);
    CHECK(decideOnEnd(EndMode::LoadNext, 3, 2, -1).action == EndAction::Stop);
}

TEST_CASE("decideOnEnd: stop, repeat-one and shuffle") {
    CHECK(decideOnEnd(EndMode::StopAtEnd, 3, 1, -1).action == EndAction::Stop);

    auto r = decideOnEnd(EndMode::RepeatOne, 3, 1, -1);
    CHECK(r.action == EndAction::Play);
    CHECK(r.index == 1);

    auto s = decideOnEnd(EndMode::Shuffle, 3, 1, 2);
    CHECK(s.action == EndAction::Play);
    CHECK(s.index == 2);
    // A candidate outside the list is not played rather than clamped.
    CHECK(decideOnEnd(EndMode::Shuffle, 3, 1, 9).action == EndAction::Nothing);
}

TEST_CASE("decideOnEnd: an empty playlist stops in every mode") {
    for (const auto& m : kEndModes) {
        CAPTURE(m.key);
        CHECK(decideOnEnd(m.mode, 0, -1, -1).action == EndAction::Stop);
    }
}

TEST_CASE("decideOnEnd: nothing playing yet") {
    // current == -1 happens after the playing item was removed (F-9).
    CHECK(decideOnEnd(EndMode::PlayNext, 3, -1, -1).index == 0);
    CHECK(decideOnEnd(EndMode::RepeatOne, 3, -1, -1).action == EndAction::Nothing);
}

TEST_CASE("Next and Previous always move, and only Loop wraps") {
    for (const auto& m : kEndModes) {
        CAPTURE(m.key);
        if (m.mode == EndMode::Shuffle) continue; // draws from the bag instead
        CHECK(decideOnNext(m.mode, 3, 0, -1).index == 1);
        CHECK(decideOnPrev(m.mode, 3, 2).index == 1);
        const bool wraps = (m.mode == EndMode::Loop);
        CHECK((decideOnNext(m.mode, 3, 2, -1).action == EndAction::Play) == wraps);
        CHECK((decideOnPrev(m.mode, 3, 0).action == EndAction::Play) == wraps);
    }
    // Next in shuffle mode plays whatever the bag drew.
    CHECK(decideOnNext(EndMode::Shuffle, 5, 0, 3).index == 3);
    // With nothing current, Next starts at the top and Previous at the bottom.
    CHECK(decideOnNext(EndMode::PlayNext, 4, -1, -1).index == 0);
    CHECK(decideOnPrev(EndMode::PlayNext, 4, -1).index == 3);
    CHECK(decideOnNext(EndMode::PlayNext, 0, -1, -1).action == EndAction::Nothing);
}
