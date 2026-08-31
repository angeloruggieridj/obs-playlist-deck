// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

namespace pld {

struct PlaylistItem {
    std::string path;
    std::string title;
    long long durationMs = -1; // -1 == unknown
};

inline bool operator==(const PlaylistItem& a, const PlaylistItem& b) {
    return a.path == b.path && a.title == b.title && a.durationMs == b.durationMs;
}

class Playlist {
public:
    const std::vector<PlaylistItem>& items() const { return items_; }
    int size() const { return static_cast<int>(items_.size()); }
    bool empty() const { return items_.empty(); }
    int currentIndex() const { return current_; }

    const PlaylistItem* current() const {
        return (current_ >= 0 && current_ < size()) ? &items_[current_] : nullptr;
    }

    void add(const PlaylistItem& it);
    bool insert(int index, const PlaylistItem& it);

    // Removing the item that is playing clears the current index instead of
    // sliding it onto whatever item takes that row. The old behaviour left the
    // model believing a different clip was playing, so the next auto-advance
    // skipped one. "Nothing is current" is the honest answer, and the dock
    // stops playback to match.
    bool removeAt(int index);

    // Removes several rows at once (multi-selection). Indices may arrive in any
    // order and out of range entries are ignored. Returns how many were removed.
    int removeMany(std::vector<int> indices);

    void clear();
    bool move(int from, int to);
    bool moveUp(int index);
    bool moveDown(int index);
    bool setCurrent(int index);

    // Renames an item (a title override; the file on disk is untouched).
    bool setTitle(int index, const std::string& title);

    int next(bool wrap);
    int prev(bool wrap);
    void setItems(std::vector<PlaylistItem> items);
    // Same, but keeping the current index when it is still in range — what the
    // dock wants after a duration or title update, where the playing item has
    // not changed.
    void setItemsKeepCurrent(std::vector<PlaylistItem> items);

    // Sum of the known durations, in milliseconds; items with an unknown
    // duration contribute nothing.
    long long totalDurationMs() const;
    // How many items still have no duration (the total above is a lower bound
    // while this is non-zero).
    int unknownDurationCount() const;

private:
    std::vector<PlaylistItem> items_;
    int current_ = -1;
};

} // namespace pld
