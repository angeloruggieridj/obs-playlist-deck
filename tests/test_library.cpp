#include "doctest/doctest.h"
#include "Library.hpp"
#include "PlaylistIO.hpp"

#include <string>

using namespace pld;

static PlaylistItem item(const std::string& name) {
    return PlaylistItem{"/media/" + name + ".mp4", name, 1000};
}

TEST_CASE("a new library holds exactly one playlist") {
    Library lib;
    CHECK(lib.count() == 1);
    CHECK(lib.activeIndex() == 0);
    CHECK(lib.active().name == "Playlist 1");
    CHECK(lib.active().items.empty());
}

TEST_CASE("adding and switching") {
    Library lib;
    CHECK(lib.add("Warm-up") == 1);
    CHECK(lib.activeIndex() == 1); // a new playlist is the one you wanted to use
    CHECK(lib.count() == 2);
    CHECK(lib.active().name == "Warm-up");

    CHECK(lib.setActive(0));
    CHECK(lib.active().name == "Playlist 1");
    CHECK_FALSE(lib.setActive(7));
    CHECK(lib.activeIndex() == 0);
}

// Two identical names in a dropdown help nobody.
TEST_CASE("names are made unique") {
    Library lib;
    lib.add("Set");
    lib.add("Set");
    lib.add("Set");
    CHECK(lib.at(1).name == "Set");
    CHECK(lib.at(2).name == "Set (2)");
    CHECK(lib.at(3).name == "Set (3)");

    // An unnamed playlist is numbered.
    const int idx = lib.add("");
    CHECK(lib.at(idx).name == "Playlist 5");
}

TEST_CASE("rename keeps names unique but leaves the playlist its own name") {
    Library lib;
    lib.add("Set");
    CHECK(lib.rename(1, "Set")); // renaming to what it already is
    CHECK(lib.at(1).name == "Set");
    CHECK(lib.rename(1, "Playlist 1"));
    CHECK(lib.at(1).name == "Playlist 1 (2)"); // the first one still has it
    CHECK_FALSE(lib.rename(1, ""));
    CHECK_FALSE(lib.rename(9, "x"));
}

TEST_CASE("duplicate copies the contents and forgets the file") {
    Library lib;
    lib.mutableActive().items = {item("a"), item("b")};
    lib.mutableActive().sourcePath = "/sets/friday.json";
    lib.mutableActive().watchFolder = "/drops";

    REQUIRE(lib.duplicate(0) == 1);
    CHECK(lib.count() == 2);
    CHECK(lib.at(1).name == "Playlist 1 (2)");
    CHECK(lib.at(1).items.size() == 2);
    CHECK(lib.at(1).watchFolder == "/drops");
    // Saving the copy must not silently overwrite the original's file.
    CHECK(lib.at(1).sourcePath.empty());
    CHECK(lib.at(0).sourcePath == "/sets/friday.json");
}

TEST_CASE("remove keeps the active index pointing at something sensible") {
    Library lib;
    lib.add("B");
    lib.add("C");
    lib.setActive(2);
    CHECK(lib.remove(0)); // removing before the active one
    CHECK(lib.count() == 2);
    CHECK(lib.active().name == "C");

    lib.setActive(1);
    CHECK(lib.remove(1)); // removing the active one
    CHECK(lib.activeIndex() == 0);
    CHECK(lib.active().name == "B");
}

// "No playlist at all" is a state the whole deck would have to defend against.
TEST_CASE("the last playlist is emptied, not removed") {
    Library lib;
    lib.mutableActive().items = {item("a")};
    lib.mutableActive().watchFolder = "/drops";
    CHECK(lib.remove(0));
    CHECK(lib.count() == 1);
    CHECK(lib.active().items.empty());
    CHECK(lib.active().watchFolder.empty());
    CHECK(lib.active().name == "Playlist 1");
    CHECK_FALSE(lib.remove(3));
}

TEST_CASE("indexOfName") {
    Library lib;
    lib.add("Stingers");
    CHECK(lib.indexOfName("Stingers") == 1);
    CHECK(lib.indexOfName("Nope") == -1);
}

TEST_CASE("library json round-trips playlists and their properties") {
    Library lib;
    lib.mutableActive().items = {item("a"), item("b")};
    lib.add("Stingers");
    lib.mutableActive().items = {item("hit")};
    lib.mutableActive().watchFolder = "/drops";
    lib.mutableActive().scheduledStartMs = 1788000000000LL;
    lib.mutableActive().sourcePath = "/sets/stingers.m3u";

    const std::string text = io::toLibraryJson(lib.entries(), lib.activeIndex());
    std::vector<PlaylistEntry> back;
    int active = -1;
    REQUIRE(io::fromLibraryJson(text, back, active));
    REQUIRE(back.size() == 2);
    CHECK(active == 1);
    CHECK(back[0].name == "Playlist 1");
    CHECK(back[0].items.size() == 2);
    CHECK(back[1].name == "Stingers");
    CHECK(back[1].items.size() == 1);
    CHECK(back[1].watchFolder == "/drops");
    CHECK(back[1].scheduledStartMs == 1788000000000LL);
    CHECK(back[1].sourcePath == "/sets/stingers.m3u");
}

// The session file written by 1.3.x is a single playlist. Nobody should lose it
// by upgrading.
TEST_CASE("a version 1 session file becomes the first playlist of the library") {
    const std::string old = R"({"version":1,"name":"session","items":[
        {"path":"/a/x.mp4","title":"X","duration":1000},
        {"path":"/a/y.mp4","title":"Y","duration":-1}]})";
    std::vector<PlaylistEntry> entries;
    int active = -1;
    REQUIRE(io::fromLibraryJson(old, entries, active));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "Playlist 1");
    CHECK(entries[0].items.size() == 2);
    CHECK(entries[0].items[0].title == "X");
    CHECK(active == 0);
}

TEST_CASE("a damaged library entry costs that entry, not the library") {
    const std::string text = R"({"version":2,"active":0,"playlists":[
        {"name":"Good","items":[{"path":"/a.mp4"},{"path":123},{"nope":true}]},
        "not an object",
        {"name":"","items":[]}]})";
    std::vector<PlaylistEntry> entries;
    int active = -1;
    REQUIRE(io::fromLibraryJson(text, entries, active));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].items.size() == 1);
    CHECK(entries[1].name.empty()); // the library names it on load
}

TEST_CASE("setEntries repairs what a hand-edited file got wrong") {
    Library lib;
    std::vector<PlaylistEntry> entries(2);
    entries[0].name = "";
    entries[1].name = "Named";
    lib.setEntries(entries, 9);
    CHECK(lib.count() == 2);
    CHECK(lib.at(0).name == "Playlist 1");
    CHECK(lib.activeIndex() == 0); // an out-of-range active index is not fatal

    lib.setEntries({}, 0);
    CHECK(lib.count() == 1); // never empty
}

TEST_CASE("nonsense is refused outright") {
    std::vector<PlaylistEntry> entries;
    int active = -1;
    CHECK_FALSE(io::fromLibraryJson("not json", entries, active));
    CHECK_FALSE(io::fromLibraryJson(R"({"playlists":42})", entries, active));
}
