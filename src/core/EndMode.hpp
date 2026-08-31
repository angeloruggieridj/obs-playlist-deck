// SPDX-License-Identifier: MIT
#pragma once

namespace pld {

// What happens when the playing clip ends.
//
// The values are persisted in settings.json, so they are fixed for good; the
// order the combo box shows them in is kEndModes below, not the numeric order,
// precisely so the two can be changed independently. Before this table the
// combo's row index *was* the enum value, and reordering the menu would have
// silently made "Stop" play a shuffle.
enum class EndMode : int {
    PlayNext = 0,
    Loop = 1,
    LoadNext = 2,
    StopAtEnd = 3,
    Shuffle = 4,
    RepeatOne = 5,
};

struct EndModeInfo {
    EndMode mode;
    const char* key;     // locale key for the combo entry
    const char* tipKey;  // locale key for its tooltip
};

inline constexpr EndModeInfo kEndModes[] = {
    {EndMode::PlayNext, "OnEnd.PlayNext", "OnEndTip.PlayNext"},
    {EndMode::Loop, "OnEnd.Loop", "OnEndTip.Loop"},
    {EndMode::LoadNext, "OnEnd.LoadNext", "OnEndTip.LoadNext"},
    {EndMode::StopAtEnd, "OnEnd.Stop", "OnEndTip.Stop"},
    {EndMode::Shuffle, "OnEnd.Shuffle", "OnEndTip.Shuffle"},
    {EndMode::RepeatOne, "OnEnd.RepeatOne", "OnEndTip.RepeatOne"},
};

inline constexpr int kEndModeCount = static_cast<int>(sizeof(kEndModes) / sizeof(kEndModes[0]));

bool isValidEndMode(int value);
EndMode endModeFromInt(int value); // falls back to PlayNext for anything unknown

// ---- End-of-clip decision ------------------------------------------------
// Pure description of what the dock should do, so all six modes are testable
// without OBS. Playing, staging and stopping stay the dock's job.
enum class EndAction {
    Nothing,
    Play,       // start `index` now
    StageNext,  // hold the last frame, load `index` once the source is off air
    Stop,
};

struct EndDecision {
    EndAction action = EndAction::Nothing;
    int index = -1;
};

// `shuffleCandidate` is the index the shuffle bag has drawn (-1 when there is
// none); it is ignored by every other mode.
EndDecision decideOnEnd(EndMode mode, int count, int current, int shuffleCandidate);

// The explicit Next / Previous buttons and hotkeys: they always move, and only
// Loop wraps around the ends.
EndDecision decideOnNext(EndMode mode, int count, int current, int shuffleCandidate);
EndDecision decideOnPrev(EndMode mode, int count, int current);

} // namespace pld
