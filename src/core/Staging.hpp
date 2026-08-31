// SPDX-License-Identifier: MIT
#pragma once

namespace pld {

// Where the bound media source sits relative to what is on air. Filled in from
// the OBS frontend API (current scene / current preview scene) whenever those
// change; "unknown" covers the case where the source is not bound at all.
struct ProgramPresence {
    bool bound = false;      // a source is bound at all
    bool inProgram = false;  // it is inside the scene currently on air
    bool studioMode = false; // OBS is in studio mode
};

// "Load next (paused)" holds the finished clip's last frame on air and loads the
// following one only once the source is off air, so the next clip's first frame
// never flashes in program.
//
// The trigger used to be the source's "deactivate" signal. OBS raises that only
// when a source is in no active scene at all — and in studio mode a source in
// the preview scene counts as active — so the ordinary program -> preview
// transition raised nothing and the staged clip was never loaded (it then
// loaded hours later, unprompted, when the source finally went inactive).
//
// This is the replacement rule, kept pure so every case is testable: a pending
// stage fires as soon as the bound source is not in the program scene.
inline bool shouldStageNow(bool pending, const ProgramPresence& presence) {
    if (!pending) return false;
    if (!presence.bound) return false;
    return !presence.inProgram;
}

// Whether the dock should tell the user a staged clip is waiting. It is only
// worth saying while the source is still on air — once it is off air the clip
// loads immediately and the notice would be stale.
inline bool shouldWarnPendingStage(bool pending, const ProgramPresence& presence) {
    return pending && presence.bound && presence.inProgram;
}

} // namespace pld
