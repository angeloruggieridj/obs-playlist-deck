// SPDX-License-Identifier: MIT
#include "EndMode.hpp"

namespace pld {

bool isValidEndMode(int value) {
    for (const auto& m : kEndModes)
        if (static_cast<int>(m.mode) == value) return true;
    return false;
}

EndMode endModeFromInt(int value) {
    return isValidEndMode(value) ? static_cast<EndMode>(value) : EndMode::PlayNext;
}

namespace {
EndDecision play(int index) { return {EndAction::Play, index}; }
EndDecision none() { return {}; }
} // namespace

EndDecision decideOnEnd(EndMode mode, int count, int current, int shuffleCandidate) {
    if (count <= 0) return {EndAction::Stop, -1};
    switch (mode) {
    case EndMode::PlayNext:
        if (current + 1 < count) return play(current + 1);
        return {EndAction::Stop, -1}; // end of list: stop rather than wrap
    case EndMode::Loop:
        return play(count == 0 ? -1 : (current + 1) % count);
    case EndMode::LoadNext: {
        const int next = (current + 1 < count) ? current + 1 : -1;
        if (next < 0) return {EndAction::Stop, -1};
        return {EndAction::StageNext, next};
    }
    case EndMode::StopAtEnd:
        return {EndAction::Stop, -1};
    case EndMode::Shuffle:
        if (shuffleCandidate < 0 || shuffleCandidate >= count) return none();
        return play(shuffleCandidate);
    case EndMode::RepeatOne:
        if (current < 0 || current >= count) return none();
        return play(current);
    }
    return none();
}

EndDecision decideOnNext(EndMode mode, int count, int current, int shuffleCandidate) {
    if (count <= 0) return none();
    if (mode == EndMode::Shuffle) {
        if (shuffleCandidate < 0 || shuffleCandidate >= count) return none();
        return play(shuffleCandidate);
    }
    if (current < 0) return play(0);
    if (current + 1 < count) return play(current + 1);
    // Only Loop wraps; every other mode stops at the last item, which is what
    // "Next" at the end of a set should do in front of an audience.
    return (mode == EndMode::Loop) ? play(0) : none();
}

EndDecision decideOnPrev(EndMode mode, int count, int current) {
    if (count <= 0) return none();
    if (current < 0) return play(count - 1);
    if (current - 1 >= 0) return play(current - 1);
    return (mode == EndMode::Loop) ? play(count - 1) : none();
}

} // namespace pld
