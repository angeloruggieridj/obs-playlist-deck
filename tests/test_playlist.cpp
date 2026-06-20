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
    CHECK(p.removeAt(1)); // remove the current (last)
    CHECK(p.currentIndex() == 0);
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
