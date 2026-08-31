// Source-inspection test: it reads the plugin sources as text and asserts on
// what it finds. The decision it pins needs a running OBS to exercise, and the
// regression it guards against is silent — the dock simply stops coming back
// where the user left it. See CONTRIBUTING.md for when to replace one of these
// with a real unit test.
#include "doctest/doctest.h"

#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readSource(const char* relativePath) {
    std::ifstream input(std::string(PLD_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Body of a function definition, from its signature to the matching closing brace.
std::string bodyOf(const std::string& source, const std::string& signature) {
    const auto start = source.find(signature);
    if (start == std::string::npos) return {};
    const auto open = source.find('{', start);
    if (open == std::string::npos) return {};
    int depth = 0;
    for (auto i = open; i < source.size(); ++i) {
        if (source[i] == '{') ++depth;
        else if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
    }
    return {};
}
}

// Regression: the dock was registered with obs_frontend_add_custom_qdock(),
// which OBS deliberately leaves unmanaged - AddCustomDockWidget() never adds the
// toggle action to the Docks menu, so there was no way to show or hide the deck
// from the UI.
TEST_CASE("the dock is registered through the OBS-managed dock API") {
    const std::string main = readSource("src/plugin/plugin-main.cpp");
    REQUIRE_FALSE(main.empty());

    CHECK(main.find("obs_frontend_add_dock_by_id") != std::string::npos);
    CHECK(main.find("obs_frontend_add_custom_qdock") == std::string::npos);
}

// obs_frontend_add_dock_by_id() takes a plain QWidget and wraps it in a dock of
// OBS's own. Deriving from QDockWidget would nest a dock inside a dock.
TEST_CASE("the dock widget is a plain QWidget") {
    const std::string hpp = readSource("src/plugin/PlaylistDock.hpp");
    REQUIRE_FALSE(hpp.empty());

    CHECK(hpp.find("class PlaylistDock : public QWidget") != std::string::npos);
    CHECK(hpp.find("class PlaylistDock : public QDockWidget") == std::string::npos);
}

// Regression: registering on OBS_FRONTEND_EVENT_FINISHED_LOADING made the deck
// reappear at every launch even after the user closed it. OBSInit() restores the
// saved dock layout right after the modules are loaded and before that event
// fires, so a dock added later gets neither its visibility nor its geometry back.
TEST_CASE("the dock is registered while modules load, not on FINISHED_LOADING") {
    const std::string main = readSource("src/plugin/plugin-main.cpp");

    const std::string load = bodyOf(main, "bool obs_module_load(void)");
    REQUIRE_FALSE(load.empty());
    CHECK(load.find("obs_frontend_add_dock_by_id") != std::string::npos);

    const std::string onEvent = bodyOf(main, "static void on_frontend_event(");
    REQUIRE_FALSE(onEvent.empty());
    CHECK(onEvent.find("obs_frontend_add_dock_by_id") == std::string::npos);
    CHECK(onEvent.find("new PlaylistDock") == std::string::npos);
}

// Registering that early means the constructor runs before any scene collection
// exists, so everything that touches sources, the session or hotkeys has to wait
// for FINISHED_LOADING instead.
TEST_CASE("source-dependent setup is deferred to FINISHED_LOADING") {
    const std::string main = readSource("src/plugin/plugin-main.cpp");
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    CHECK(bodyOf(main, "static void on_frontend_event(").find("frontendLoaded()") !=
          std::string::npos);

    const std::string loaded = bodyOf(dock, "void PlaylistDock::frontendLoaded()");
    REQUIRE_FALSE(loaded.empty());
    CHECK(loaded.find("refreshSources()") != std::string::npos);
    CHECK(loaded.find("loadSession()") != std::string::npos);
    CHECK(loaded.find("clearStalePluginFile()") != std::string::npos);
    CHECK(loaded.find("registerHotkeys()") != std::string::npos);

    const std::string ctor = bodyOf(dock, "PlaylistDock::PlaylistDock(QWidget* parent)");
    REQUIRE_FALSE(ctor.empty());
    CHECK(ctor.find("refreshSources()") == std::string::npos);
    CHECK(ctor.find("loadSession()") == std::string::npos);
    CHECK(ctor.find("registerHotkeys()") == std::string::npos);
}
