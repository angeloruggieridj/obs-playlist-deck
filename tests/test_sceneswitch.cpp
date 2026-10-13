#include "doctest/doctest.h"
#include "SceneSwitch.hpp"

#include <string>

using namespace pld;

TEST_CASE("every effect maps to a distinct OBS transition kind") {
    CHECK(std::string(transitionIdFor(SwitchEffect::Cut)) == "cut_transition");
    CHECK(std::string(transitionIdFor(SwitchEffect::Fade)) == "fade_transition");
    CHECK(std::string(transitionIdFor(SwitchEffect::Stinger)) == "obs_stinger_transition");
}

TEST_CASE("the persisted values are fixed, and unknown ones fall back to a cut") {
    CHECK(static_cast<int>(SwitchEffect::Cut) == 0);
    CHECK(static_cast<int>(SwitchEffect::Fade) == 1);
    CHECK(static_cast<int>(SwitchEffect::Stinger) == 2);
    CHECK(switchEffectFromInt(2) == SwitchEffect::Stinger);
    CHECK(switchEffectFromInt(-1) == SwitchEffect::Cut);
    CHECK(switchEffectFromInt(99) == SwitchEffect::Cut);
    CHECK_FALSE(isValidSwitchEffect(3));
}

TEST_CASE("the automatic switch is off unless it is enabled and has a scene") {
    AutoSwitchContext ctx;
    ctx.sourceInProgram = true;
    CHECK_FALSE(shouldAutoSwitch(ctx)); // default: off

    ctx.enabled = true;
    CHECK_FALSE(shouldAutoSwitch(ctx)); // on, but no scene picked

    ctx.sceneChosen = true;
    CHECK(shouldAutoSwitch(ctx));
}

TEST_CASE("an item that ends off air does not change what the audience sees") {
    AutoSwitchContext ctx;
    ctx.enabled = true;
    ctx.sceneChosen = true;
    ctx.sourceInProgram = false;
    CHECK_FALSE(shouldAutoSwitch(ctx));
}

TEST_CASE("nothing happens when the target scene is already on air") {
    AutoSwitchContext ctx;
    ctx.enabled = true;
    ctx.sceneChosen = true;
    ctx.sourceInProgram = true;
    ctx.alreadyThere = true;
    CHECK_FALSE(shouldAutoSwitch(ctx));
}
