#include "doctest/doctest.h"

#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readSource(const char* relativePath) {
    std::ifstream input(std::string(PLD_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

TEST_CASE("scene collection changes release the selected OBS source before unload") {
    const std::string frontend = readSource("src/plugin/plugin-main.cpp");
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    const auto changing = frontend.find("case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:");
    const auto release = frontend.find("g_dock->releaseSource();", changing);

    REQUIRE(changing != std::string::npos);
    REQUIRE(release != std::string::npos);
    CHECK(dock.find("void PlaylistDock::releaseSource()") != std::string::npos);
    CHECK(dock.find("controller_.unbind();", dock.find("void PlaylistDock::releaseSource()")) !=
          std::string::npos);
}
