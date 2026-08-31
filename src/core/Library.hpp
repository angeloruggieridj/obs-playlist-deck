// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include "Playlist.hpp"

namespace pld {

// One named playlist in the library, with the properties that belong to it
// rather than to the deck as a whole.
struct PlaylistEntry {
    std::string name;
    std::vector<PlaylistItem> items;
    // The file this playlist was opened from or last saved to, if any.
    std::string sourcePath;
    // NF-5: a folder whose new media files are appended automatically. Empty
    // means no watch.
    std::string watchFolder;
    // NF-6: wall-clock start, as milliseconds since the epoch. -1 means the
    // playlist starts when someone presses play, like every other playlist.
    long long scheduledStartMs = -1;
};

// The set of playlists the deck knows about, and which one is active.
//
// A show is rarely one list: it is a warm-up set, the main set, and a folder of
// stingers. Keeping them in one deck means switching without opening files, and
// it is the reason several later features (a watch folder, a scheduled start)
// belong to a playlist rather than to the plugin.
//
// The library always holds at least one playlist: "no playlist at all" is a
// state the rest of the deck would have to defend against everywhere, for no
// benefit to anyone.
class Library {
public:
    Library(); // starts with one empty playlist

    int count() const { return static_cast<int>(entries_.size()); }
    int activeIndex() const { return active_; }
    bool setActive(int index);

    const std::vector<PlaylistEntry>& entries() const { return entries_; }
    const PlaylistEntry& at(int index) const;
    PlaylistEntry& mutableAt(int index);
    const PlaylistEntry& active() const { return at(active_); }
    PlaylistEntry& mutableActive() { return mutableAt(active_); }

    // Appends a playlist and makes it active. An empty or duplicate name is
    // resolved to a free one ("Playlist 2", "Set 1 (2)"), because two lists with
    // the same name in a dropdown help nobody.
    int add(const std::string& name);
    // Copies a playlist, contents and all, next to the original.
    int duplicate(int index);
    bool rename(int index, const std::string& name);
    // Removes a playlist. The last one is emptied instead of removed, so the
    // invariant above holds. Returns false when the index is out of range.
    bool remove(int index);
    // Index of the playlist with this name, or -1.
    int indexOfName(const std::string& name) const;

    // Replaces the active playlist's items — how the dock writes its live model
    // back before switching away.
    void setActiveItems(std::vector<PlaylistItem> items);

    void setEntries(std::vector<PlaylistEntry> entries, int active);

    // A name that is not taken yet, derived from `wanted` (or from the count
    // when `wanted` is empty).
    std::string freeName(const std::string& wanted, int ignoreIndex = -1) const;

private:
    std::vector<PlaylistEntry> entries_;
    int active_ = 0;
};

} // namespace pld
