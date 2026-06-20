#include "doctest/doctest.h"
#include "Format.hpp"
using namespace pld;

TEST_CASE("formatDuration") {
    CHECK(formatDuration(-1) == "");
    CHECK(formatDuration(0) == "0:00");
    CHECK(formatDuration(1000) == "0:01");
    CHECK(formatDuration(62000) == "1:02");
    CHECK(formatDuration(3723000) == "1:02:03");
}
