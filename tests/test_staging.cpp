#include "doctest/doctest.h"
#include "Staging.hpp"
using namespace pld;

// F-6: "Load next (paused)" hung on the source's "deactivate" signal, which OBS
// raises only when a source is in no active scene at all. In studio mode a
// source sitting in the preview scene still counts as active, so the ordinary
// program -> preview transition raised nothing: the staged clip was never
// loaded, and then loaded itself much later, unprompted, when the source
// finally went inactive. Being out of Program is the actual condition.
TEST_CASE("a staged clip loads as soon as the source is out of program") {
    ProgramPresence p;
    p.bound = true;
    p.studioMode = true;

    p.inProgram = true;
    CHECK_FALSE(shouldStageNow(true, p)); // still on air: hold the last frame
    CHECK(shouldWarnPendingStage(true, p));

    p.inProgram = false;
    CHECK(shouldStageNow(true, p)); // off air: safe to load the next clip
    CHECK_FALSE(shouldWarnPendingStage(true, p));
}

TEST_CASE("nothing is staged without a pending request or a bound source") {
    ProgramPresence p;
    p.bound = true;
    p.inProgram = false;
    CHECK_FALSE(shouldStageNow(false, p));

    p.bound = false;
    CHECK_FALSE(shouldStageNow(true, p));
    CHECK_FALSE(shouldWarnPendingStage(true, p));
}

TEST_CASE("the rule is the same outside studio mode") {
    // Without studio mode the source leaves program by a plain scene change.
    ProgramPresence p;
    p.bound = true;
    p.studioMode = false;
    p.inProgram = true;
    CHECK_FALSE(shouldStageNow(true, p));
    p.inProgram = false;
    CHECK(shouldStageNow(true, p));
}
