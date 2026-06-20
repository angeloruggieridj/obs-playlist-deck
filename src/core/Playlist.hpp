#pragma once
#include <string>
#include <vector>

namespace pld {

struct PlaylistItem {
    std::string path;
    std::string title;
};

inline bool operator==(const PlaylistItem& a, const PlaylistItem& b) {
    return a.path == b.path && a.title == b.title;
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
    bool removeAt(int index);
    void clear();
    bool move(int from, int to);
    bool moveUp(int index);
    bool moveDown(int index);
    bool setCurrent(int index);
    int next(bool wrap);
    int prev(bool wrap);
    void setItems(std::vector<PlaylistItem> items);

private:
    std::vector<PlaylistItem> items_;
    int current_ = -1;
};

} // namespace pld
