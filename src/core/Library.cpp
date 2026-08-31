// SPDX-License-Identifier: MIT
#include "Library.hpp"
#include <algorithm>

namespace pld {

namespace {
// Used only when a caller asks for a playlist without naming it. Deliberately
// not localized: the name is stored, and a stored name must not change meaning
// when the user switches the UI language.
constexpr const char* kDefaultName = "Playlist";
} // namespace

Library::Library() { entries_.push_back(PlaylistEntry{std::string(kDefaultName) + " 1"}); }

bool Library::setActive(int index) {
    if (index < 0 || index >= count()) return false;
    active_ = index;
    return true;
}

const PlaylistEntry& Library::at(int index) const {
    if (index < 0 || index >= count()) return entries_.front();
    return entries_[static_cast<size_t>(index)];
}

PlaylistEntry& Library::mutableAt(int index) {
    if (index < 0 || index >= count()) return entries_.front();
    return entries_[static_cast<size_t>(index)];
}

int Library::indexOfName(const std::string& name) const {
    for (int i = 0; i < count(); ++i)
        if (entries_[static_cast<size_t>(i)].name == name) return i;
    return -1;
}

std::string Library::freeName(const std::string& wanted, int ignoreIndex) const {
    auto taken = [&](const std::string& candidate) {
        for (int i = 0; i < count(); ++i) {
            if (i == ignoreIndex) continue;
            if (entries_[static_cast<size_t>(i)].name == candidate) return true;
        }
        return false;
    };

    if (wanted.empty()) {
        // "Playlist 3" for the third one, unless that is taken too.
        for (int n = count() + 1;; ++n) {
            const std::string candidate = std::string(kDefaultName) + " " + std::to_string(n);
            if (!taken(candidate)) return candidate;
        }
    }
    if (!taken(wanted)) return wanted;
    for (int n = 2;; ++n) {
        const std::string candidate = wanted + " (" + std::to_string(n) + ")";
        if (!taken(candidate)) return candidate;
    }
}

int Library::add(const std::string& name) {
    entries_.push_back(PlaylistEntry{freeName(name)});
    active_ = count() - 1;
    return active_;
}

int Library::duplicate(int index) {
    if (index < 0 || index >= count()) return -1;
    PlaylistEntry copy = entries_[static_cast<size_t>(index)];
    copy.name = freeName(copy.name);
    // The copy is a new list, not a second window onto the same file: saving it
    // must not silently overwrite the original.
    copy.sourcePath.clear();
    entries_.insert(entries_.begin() + index + 1, std::move(copy));
    active_ = index + 1;
    return active_;
}

bool Library::rename(int index, const std::string& name) {
    if (index < 0 || index >= count() || name.empty()) return false;
    entries_[static_cast<size_t>(index)].name = freeName(name, index);
    return true;
}

bool Library::remove(int index) {
    if (index < 0 || index >= count()) return false;
    if (count() == 1) {
        // Emptied rather than removed: the library always holds one playlist.
        entries_.front() = PlaylistEntry{std::string(kDefaultName) + " 1"};
        active_ = 0;
        return true;
    }
    entries_.erase(entries_.begin() + index);
    if (active_ >= count()) active_ = count() - 1;
    else if (index < active_) --active_;
    return true;
}

void Library::setActiveItems(std::vector<PlaylistItem> items) {
    mutableActive().items = std::move(items);
}

void Library::setEntries(std::vector<PlaylistEntry> entries, int active) {
    if (entries.empty()) {
        entries_.assign(1, PlaylistEntry{std::string(kDefaultName) + " 1"});
        active_ = 0;
        return;
    }
    entries_ = std::move(entries);
    // Names come from a file that a person may well have edited by hand.
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name.empty())
            entries_[i].name = std::string(kDefaultName) + " " + std::to_string(i + 1);
    }
    active_ = (active >= 0 && active < count()) ? active : 0;
}

} // namespace pld
