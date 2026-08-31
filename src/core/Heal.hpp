// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

namespace pld {

// Finding a media file that moved.
//
// Files get reorganised between shows, and a playlist full of red "file not
// found" rows is both alarming and tedious to repair by hand. The deck knows
// the file's name, and it knows where else its files live; that is usually
// enough to find it again.
//
// The matching itself is pure so it can be tested: the caller supplies the
// candidate paths it found on disk.
struct HealMatch {
    std::string path;       // the candidate that matched, empty when none did
    bool ambiguous = false; // more than one candidate matched by name
};

// Matches on file name, case-insensitively, ignoring the folder. A single match
// is a repair; several matches are reported as ambiguous rather than guessed
// at, because picking the wrong "intro.mp4" is worse than saying nothing.
HealMatch findMoved(const std::string& missingPath, const std::vector<std::string>& candidates);

} // namespace pld
