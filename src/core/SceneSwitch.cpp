// SPDX-License-Identifier: GPL-2.0-or-later
#include "SceneSwitch.hpp"

namespace pld {

bool isValidSwitchEffect(int value) { return value >= 0 && value < kSwitchEffectCount; }

SwitchEffect switchEffectFromInt(int value) {
    return isValidSwitchEffect(value) ? static_cast<SwitchEffect>(value) : SwitchEffect::Cut;
}

const char* transitionIdFor(SwitchEffect effect) {
    switch (effect) {
    case SwitchEffect::Cut:
        return "cut_transition";
    case SwitchEffect::Fade:
        return "fade_transition";
    case SwitchEffect::Stinger:
        return "obs_stinger_transition";
    }
    return "cut_transition";
}

const char* switchEffectKey(SwitchEffect effect) {
    switch (effect) {
    case SwitchEffect::Cut:
        return "Effect.Cut";
    case SwitchEffect::Fade:
        return "Effect.Fade";
    case SwitchEffect::Stinger:
        return "Effect.Stinger";
    }
    return "Effect.Cut";
}

bool shouldAutoSwitch(const AutoSwitchContext& ctx) {
    if (!ctx.enabled || !ctx.sceneChosen) return false;
    if (!ctx.sourceInProgram) return false;
    return !ctx.alreadyThere;
}

} // namespace pld
