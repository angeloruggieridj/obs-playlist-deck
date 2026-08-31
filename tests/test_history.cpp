#include "doctest/doctest.h"
#include "History.hpp"
#include <string>
using namespace pld;

static std::vector<PlaylistItem> items(std::initializer_list<const char*> paths) {
    std::vector<PlaylistItem> out;
    for (auto p : paths) out.push_back(PlaylistItem{p, p});
    return out;
}

TEST_CASE("undo restores the state recorded before an edit") {
    History h;
    auto before = items({"a", "b", "c"});
    h.push(before, 1, "remove");

    auto now = items({"a", "c"});
    int current = -1;
    std::string label;
    REQUIRE(h.undo(now, current, label));
    CHECK(label == "remove");
    CHECK(now == before);
    CHECK(current == 1);
    CHECK_FALSE(h.canUndo());
    CHECK(h.canRedo());
}

TEST_CASE("redo replays what was undone") {
    History h;
    h.push(items({"a", "b"}), 0, "clear");
    auto state = items({});
    int current = -1;
    std::string label;
    REQUIRE(h.undo(state, current, label));
    CHECK(state.size() == 2);
    REQUIRE(h.redo(state, current, label));
    CHECK(state.empty());
    CHECK(current == -1);
    CHECK(h.canUndo());
    CHECK_FALSE(h.canRedo());
}

TEST_CASE("a new edit discards the redo branch") {
    History h;
    h.push(items({"a"}), -1, "add");
    auto state = items({"a", "b"});
    int current = -1;
    std::string label;
    REQUIRE(h.undo(state, current, label));
    CHECK(h.canRedo());
    h.push(state, current, "other edit");
    CHECK_FALSE(h.canRedo());
}

TEST_CASE("the stack is bounded, dropping the oldest entry") {
    History h(3);
    for (int i = 0; i < 10; ++i) h.push(items({"x"}), i, "edit " + std::to_string(i));
    CHECK(h.depth() == 3);
    CHECK(h.undoLabel() == "edit 9");
}

TEST_CASE("undo and redo on an empty history change nothing") {
    History h;
    auto state = items({"a"});
    int current = 0;
    std::string label = "untouched";
    CHECK_FALSE(h.undo(state, current, label));
    CHECK_FALSE(h.redo(state, current, label));
    CHECK(state.size() == 1);
    CHECK(current == 0);
    CHECK(label == "untouched");
    CHECK(h.undoLabel().empty());
}

TEST_CASE("clear drops both stacks") {
    History h;
    h.push(items({"a"}), 0, "e");
    h.clear();
    CHECK_FALSE(h.canUndo());
    CHECK_FALSE(h.canRedo());
}
