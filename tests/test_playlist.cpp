#include "doctest/doctest.h"
#include "Playlist.hpp"
using namespace pld;

static PlaylistItem mk(const std::string& p) { return PlaylistItem{p, p}; }

TEST_CASE("add appends items") {
    Playlist p;
    p.add(mk("a"));
    p.add(mk("b"));
    CHECK(p.size() == 2);
    CHECK(p.items()[0].path == "a");
    CHECK(p.currentIndex() == -1);
}

TEST_CASE("setCurrent and current()") {
    Playlist p;
    p.add(mk("a"));
    p.add(mk("b"));
    CHECK(p.setCurrent(1));
    CHECK(p.currentIndex() == 1);
    CHECK(p.current() != nullptr);
    CHECK(p.current()->path == "b");
    CHECK_FALSE(p.setCurrent(5));
    CHECK(p.setCurrent(-1));
    CHECK(p.current() == nullptr);
}

TEST_CASE("removeAt adjusts current") {
    Playlist p;
    for (auto c : {"a", "b", "c"}) p.add(mk(c));
    p.setCurrent(2);
    CHECK(p.removeAt(0)); // remove before current
    CHECK(p.currentIndex() == 1);
    CHECK(p.current()->path == "c");
    p.setCurrent(1);
    CHECK(p.removeAt(1)); // remove the current one: nothing is current after it
    CHECK(p.currentIndex() == -1);
    p.removeAt(0);
    CHECK(p.empty());
    CHECK(p.currentIndex() == -1);
}

TEST_CASE("next/prev with and without wrap") {
    Playlist p;
    for (auto c : {"a", "b"}) p.add(mk(c));
    CHECK(p.next(false) == 0);
    CHECK(p.next(false) == 1);
    CHECK(p.next(false) == -1);   // at end, no wrap
    CHECK(p.currentIndex() == 1); // unchanged
    CHECK(p.next(true) == 0);     // wrap
    CHECK(p.prev(false) == -1);   // at first, no wrap
    CHECK(p.prev(true) == 1);     // wrap to last
}

TEST_CASE("move reorders and tracks current") {
    Playlist p;
    for (auto c : {"a", "b", "c"}) p.add(mk(c));
    p.setCurrent(0);    // current = "a"
    CHECK(p.move(0, 2)); // a -> end: [b,c,a]
    CHECK(p.items()[2].path == "a");
    CHECK(p.current()->path == "a"); // current followed
    CHECK(p.moveUp(0) == false);     // already top
    CHECK(p.moveDown(2) == false);   // already bottom
}

TEST_CASE("setItems resets current") {
    Playlist p;
    p.add(mk("a"));
    p.setCurrent(0);
    p.setItems({mk("x"), mk("y")});
    CHECK(p.size() == 2);
    CHECK(p.currentIndex() == -1);
}

// F-9: removing the item that is playing used to slide `current` onto whatever
// item inherited that row, so the next auto-advance started from the wrong
// place and appeared to skip an item. Nothing is current after that removal.
TEST_CASE("removing the current item clears the current index") {
    Playlist p;
    for (auto c : {"a", "b", "c"}) p.add(mk(c));
    p.setCurrent(1);
    CHECK(p.removeAt(1));
    CHECK(p.currentIndex() == -1);
    CHECK(p.current() == nullptr);
    CHECK(p.size() == 2);
    CHECK(p.items()[1].path == "c"); // the rest kept their order
}

TEST_CASE("removeMany handles unsorted, duplicate and out-of-range indices") {
    Playlist p;
    for (auto c : {"a", "b", "c", "d", "e"}) p.add(mk(c));
    p.setCurrent(4);
    CHECK(p.removeMany({3, 0, 0, 99, -2}) == 2);
    CHECK(p.size() == 3);
    CHECK(p.items()[0].path == "b");
    CHECK(p.items()[2].path == "e");
    CHECK(p.currentIndex() == 2); // "e" moved down by the two removals before it
}

TEST_CASE("removeMany that includes the current item clears it") {
    Playlist p;
    for (auto c : {"a", "b", "c"}) p.add(mk(c));
    p.setCurrent(1);
    CHECK(p.removeMany({0, 1}) == 2);
    CHECK(p.currentIndex() == -1);
    CHECK(p.size() == 1);
}

TEST_CASE("setTitle overrides the label without touching the path") {
    Playlist p;
    p.add(mk("/a/clip 01.mp4"));
    CHECK(p.setTitle(0, "Intro"));
    CHECK(p.items()[0].title == "Intro");
    CHECK(p.items()[0].path == "/a/clip 01.mp4");
    CHECK_FALSE(p.setTitle(0, "Intro")); // no change, no edit
    CHECK_FALSE(p.setTitle(0, ""));      // an empty title is not a rename
    CHECK_FALSE(p.setTitle(7, "x"));
}

TEST_CASE("setItemsKeepCurrent keeps a still-valid current index") {
    Playlist p;
    for (auto c : {"a", "b", "c"}) p.add(mk(c));
    p.setCurrent(2);
    auto items = p.items();
    items[2].durationMs = 1000;
    p.setItemsKeepCurrent(std::move(items));
    CHECK(p.currentIndex() == 2);
    CHECK(p.items()[2].durationMs == 1000);

    p.setItemsKeepCurrent({mk("only")});
    CHECK(p.currentIndex() == -1); // out of range now, so nothing is current
}

TEST_CASE("totals ignore unknown durations but count them") {
    Playlist p;
    p.add(PlaylistItem{"a", "a", 60000});
    p.add(PlaylistItem{"b", "b", 30000});
    p.add(PlaylistItem{"c", "c", -1});
    CHECK(p.totalDurationMs() == 90000);
    CHECK(p.unknownDurationCount() == 1);
}
