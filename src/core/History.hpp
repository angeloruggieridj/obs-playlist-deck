// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "Playlist.hpp"

namespace pld {

// Undo/redo for playlist edits, as snapshots rather than commands.
//
// A playlist is a vector of small structs: a whole copy costs about as much as
// describing the change would, and a snapshot cannot drift out of sync with the
// model the way an inverse command can. Playback is deliberately not undoable —
// undoing "play" in front of an audience would be a surprise, not a rescue.
class History {
public:
    struct Snapshot {
        std::vector<PlaylistItem> items;
        int current = -1;
        std::string label; // localized description of the edit, for the status line
    };

    explicit History(size_t limit = 50) : limit_(limit) {}

    // Records the state *before* an edit, together with what the edit was.
    void push(const std::vector<PlaylistItem>& items, int current, const std::string& label);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    const std::string& undoLabel() const;
    const std::string& redoLabel() const;

    // Swap the given state with the top of the undo stack (moving it onto redo).
    // Returns false, leaving everything untouched, when there is nothing to undo.
    bool undo(std::vector<PlaylistItem>& items, int& current, std::string& labelOut);
    bool redo(std::vector<PlaylistItem>& items, int& current, std::string& labelOut);

    void clear();
    size_t depth() const { return undo_.size(); }

private:
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    size_t limit_;
};

} // namespace pld
