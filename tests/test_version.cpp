#include "doctest/doctest.h"
#include "Version.hpp"
using namespace pld;

TEST_CASE("isNewerVersion") {
    CHECK(isNewerVersion("1.1.0", "1.0.0"));
    CHECK(isNewerVersion("v1.0.1", "1.0.0"));
    CHECK(isNewerVersion("2.0", "1.9.9"));
    CHECK_FALSE(isNewerVersion("1.0.0", "1.0.0"));
    CHECK_FALSE(isNewerVersion("1.0.0", "1.1.0"));
    CHECK_FALSE(isNewerVersion("v1.0.0", "1.0"));
    CHECK(isNewerVersion("1.0.1", "v1.0"));
}
