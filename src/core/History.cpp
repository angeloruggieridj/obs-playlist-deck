// SPDX-License-Identifier: MIT
#include "History.hpp"

namespace pld {

namespace {
const std::string kEmpty;
}

void History::push(const std::vector<PlaylistItem>& items, int current, const std::string& label) {
    undo_.push_back(Snapshot{items, current, label});
    // A new edit invalidates anything that was undone: the timeline branched.
    redo_.clear();
    if (limit_ > 0 && undo_.size() > limit_) undo_.erase(undo_.begin());
}

const std::string& History::undoLabel() const {
    return undo_.empty() ? kEmpty : undo_.back().label;
}

const std::string& History::redoLabel() const {
    return redo_.empty() ? kEmpty : redo_.back().label;
}

bool History::undo(std::vector<PlaylistItem>& items, int& current, std::string& labelOut) {
    if (undo_.empty()) return false;
    Snapshot prev = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(Snapshot{items, current, prev.label});
    items = std::move(prev.items);
    current = prev.current;
    labelOut = prev.label;
    return true;
}

bool History::redo(std::vector<PlaylistItem>& items, int& current, std::string& labelOut) {
    if (redo_.empty()) return false;
    Snapshot next = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(Snapshot{items, current, next.label});
    items = std::move(next.items);
    current = next.current;
    labelOut = next.label;
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace pld
