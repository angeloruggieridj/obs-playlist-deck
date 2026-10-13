// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace pld {

// The effect used for the optional "switch scene when an item ends" action.
//
// The values are persisted in settings.json, so they are fixed for good, like
// EndMode's.
enum class SwitchEffect : int {
    Cut = 0,
    Fade = 1,
    Stinger = 2,
};

inline constexpr int kSwitchEffectCount = 3;

bool isValidSwitchEffect(int value);
SwitchEffect switchEffectFromInt(int value); // falls back to Cut for anything unknown

// The id of the OBS transition kind that produces the effect.
const char* transitionIdFor(SwitchEffect effect);

// Locale key that names the effect in the Settings dialog.
const char* switchEffectKey(SwitchEffect effect);

// What the dock knows when an item has just ended.
struct AutoSwitchContext {
    bool enabled = false;       // the user switched the feature on
    bool sceneChosen = false;   // ...and picked a scene to go to
    bool sourceInProgram = false; // the bound source is part of what is on air
    bool alreadyThere = false;  // the target scene is already the program scene
};

// Whether the end of an item should cut away. Off unless asked for; and even
// then only when the clip was on air - an item that finishes in a scene nobody
// is looking at has no business changing what the audience sees.
bool shouldAutoSwitch(const AutoSwitchContext& ctx);

} // namespace pld
