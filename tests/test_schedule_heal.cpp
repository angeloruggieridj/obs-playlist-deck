#include "doctest/doctest.h"
#include "Heal.hpp"
#include "Schedule.hpp"

#include <string>
#include <vector>

using namespace pld;

namespace {
constexpr long long kNow = 1788000000000LL; // an arbitrary "now", in epoch ms
constexpr long long kSecond = 1000;
} // namespace

TEST_CASE("nothing scheduled says so") {
    const auto s = scheduleStatus(-1, kNow);
    CHECK(s.state == ScheduleState::None);
    CHECK(s.remainingMs == 0);
}

TEST_CASE("a start far away is waited for, and counted down") {
    const auto s = scheduleStatus(kNow + 90 * kSecond, kNow);
    CHECK(s.state == ScheduleState::Waiting);
    CHECK(s.remainingMs == 90 * kSecond);
}

TEST_CASE("inside the warning window the operator is told") {
    CHECK(scheduleStatus(kNow + 11 * kSecond, kNow).state == ScheduleState::Waiting);
    CHECK(scheduleStatus(kNow + 10 * kSecond, kNow).state == ScheduleState::Warning);
    CHECK(scheduleStatus(kNow + 1 * kSecond, kNow).state == ScheduleState::Warning);
    // The window is configurable, because 10 seconds is not everyone's cue.
    CHECK(scheduleStatus(kNow + 25 * kSecond, kNow, 30).state == ScheduleState::Warning);
}

TEST_CASE("the moment itself, and a moment that has passed") {
    CHECK(scheduleStatus(kNow, kNow).state == ScheduleState::Due);
    // OBS may have been closed, or the operator may have typed 21:30 at 21:31
    // meaning "now". Either way the answer is to start, not to ignore it.
    const auto late = scheduleStatus(kNow - 3600 * kSecond, kNow);
    CHECK(late.state == ScheduleState::Due);
    CHECK(late.remainingMs == 0);
}

// The one thing a schedule must never do.
TEST_CASE("a schedule fires once") {
    long long lastFired = -1;
    const long long start = kNow;
    CHECK(shouldFireSchedule(start, kNow, lastFired));
    lastFired = start;
    CHECK_FALSE(shouldFireSchedule(start, kNow, lastFired));
    CHECK_FALSE(shouldFireSchedule(start, kNow + 60 * kSecond, lastFired));
    // Setting a different time arms it again.
    CHECK(shouldFireSchedule(start + 120 * kSecond, kNow + 200 * kSecond, lastFired));
}

TEST_CASE("nothing fires without a schedule, or before its time") {
    CHECK_FALSE(shouldFireSchedule(-1, kNow, -1));
    CHECK_FALSE(shouldFireSchedule(kNow + kSecond, kNow, -1));
}

// ---- finding a file that moved -------------------------------------------

TEST_CASE("a single match by name is the repair") {
    const std::vector<std::string> candidates = {"/new/place/intro.mp4", "/new/place/outro.mp4"};
    const auto match = findMoved("/old/place/intro.mp4", candidates);
    CHECK(match.path == "/new/place/intro.mp4");
    CHECK_FALSE(match.ambiguous);
}

TEST_CASE("the match ignores case and folders") {
    const auto match = findMoved("/old/Intro.MP4", {"/somewhere/else/intro.mp4"});
    CHECK(match.path == "/somewhere/else/intro.mp4");
}

// Picking the wrong "intro.mp4" in front of an audience is worse than saying
// nothing at all.
TEST_CASE("two files with the same name are reported, not guessed at") {
    const auto match =
        findMoved("/old/intro.mp4", {"/a/intro.mp4", "/b/intro.mp4", "/a/other.mp4"});
    CHECK(match.path.empty());
    CHECK(match.ambiguous);
}

TEST_CASE("the same candidate offered twice is one match") {
    const auto match = findMoved("/old/intro.mp4", {"/a/intro.mp4", "/a/intro.mp4"});
    CHECK(match.path == "/a/intro.mp4");
    CHECK_FALSE(match.ambiguous);
}

TEST_CASE("no match, and the path we already know is gone") {
    CHECK(findMoved("/old/intro.mp4", {"/a/other.mp4"}).path.empty());
    // The missing path itself is not a repair for itself.
    CHECK(findMoved("/old/intro.mp4", {"/old/intro.mp4"}).path.empty());
    CHECK(findMoved("", {"/a/intro.mp4"}).path.empty());
    CHECK(findMoved("/old/intro.mp4", {}).path.empty());
}
