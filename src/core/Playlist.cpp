// SPDX-License-Identifier: MIT
#include "Playlist.hpp"
#include <algorithm>

namespace pld {

void Playlist::add(const PlaylistItem& it) { items_.push_back(it); }

bool Playlist::insert(int index, const PlaylistItem& it) {
    if (index < 0 || index > size()) return false;
    items_.insert(items_.begin() + index, it);
    if (current_ >= index) ++current_;
    return true;
}

bool Playlist::removeAt(int index) {
    if (index < 0 || index >= size()) return false;
    items_.erase(items_.begin() + index);
    if (empty()) { current_ = -1; }
    else if (index < current_) { --current_; }
    else if (index == current_) { current_ = -1; } // the playing item is gone
    return true;
}

int Playlist::removeMany(std::vector<int> indices) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    int removed = 0;
    // Back to front, so each erase leaves the earlier indices meaning the same
    // rows they did when the selection was taken.
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        if (removeAt(*it)) ++removed;
    return removed;
}

void Playlist::clear() { items_.clear(); current_ = -1; }

bool Playlist::move(int from, int to) {
    if (from < 0 || from >= size() || to < 0 || to >= size()) return false;
    if (from == to) return true;
    PlaylistItem it = items_[from];
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, it);
    if (current_ == from) {
        current_ = to;
    } else {
        if (from < current_) --current_;
        if (to <= current_) ++current_;
    }
    return true;
}

bool Playlist::moveUp(int index) { return index > 0 && move(index, index - 1); }
bool Playlist::moveDown(int index) { return index >= 0 && index < size() - 1 && move(index, index + 1); }

bool Playlist::setCurrent(int index) {
    if (index == -1) { current_ = -1; return true; }
    if (index < 0 || index >= size()) return false;
    current_ = index;
    return true;
}

bool Playlist::setTitle(int index, const std::string& title) {
    if (index < 0 || index >= size() || title.empty()) return false;
    if (items_[index].title == title) return false;
    items_[index].title = title;
    return true;
}

int Playlist::next(bool wrap) {
    if (empty()) return -1;
    if (current_ == -1) { current_ = 0; return current_; }
    if (current_ + 1 < size()) { ++current_; return current_; }
    if (wrap) { current_ = 0; return current_; }
    return -1;
}

int Playlist::prev(bool wrap) {
    if (empty()) return -1;
    if (current_ == -1) { current_ = size() - 1; return current_; }
    if (current_ - 1 >= 0) { --current_; return current_; }
    if (wrap) { current_ = size() - 1; return current_; }
    return -1;
}

void Playlist::setItems(std::vector<PlaylistItem> items) {
    items_ = std::move(items);
    current_ = -1;
}

void Playlist::setItemsKeepCurrent(std::vector<PlaylistItem> items) {
    const int keep = current_;
    items_ = std::move(items);
    current_ = (keep >= 0 && keep < size()) ? keep : -1;
}

long long Playlist::totalDurationMs() const {
    long long total = 0;
    for (const auto& it : items_)
        if (it.durationMs > 0) total += it.durationMs;
    return total;
}

int Playlist::unknownDurationCount() const {
    int n = 0;
    for (const auto& it : items_)
        if (it.durationMs < 0) ++n;
    return n;
}

} // namespace pld
