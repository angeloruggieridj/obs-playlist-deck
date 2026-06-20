#include "doctest/doctest.h"
#include "Playlist.hpp"

TEST_CASE("empty playlist has no current item") {
    pld::Playlist p;
    CHECK(p.empty());
    CHECK(p.size() == 0);
    CHECK(p.currentIndex() == -1);
}
