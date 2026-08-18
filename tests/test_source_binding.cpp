#include "doctest/doctest.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

// Regression: switching scene collection used to leave the source combo on
// whichever media source happened to be first in the incoming collection
// (QComboBox selects index 0 as soon as an item is added to an empty box). The
// dock then bound that arbitrary source and cleared its local_file, wiping a
// path the user had configured in OBS.
TEST_CASE("refreshSources never auto-binds an arbitrary source") {
    const std::string body = bodyOf(readSource("src/plugin/PlaylistDock.cpp"),
                                    "void PlaylistDock::refreshSources()");
    REQUIRE_FALSE(body.empty());

    // A placeholder occupies index 0, so "nothing configured" is representable.
    CHECK(body.find("addItem(T(\"NoSourceConfigured\"), QString())") != std::string::npos);
    // Selection is resolved by item data (the source name), not display text.
    CHECK(body.find("findData(wanted)") != std::string::npos);
    // An unresolved name falls back to the placeholder, never to a real source.
    CHECK(body.find("setCurrentIndex(idx >= 0 ? idx : 0)") != std::string::npos);
    CHECK(body.find("findText(") == std::string::npos);
}

TEST_CASE("a programmatic refresh does not overwrite the configured source") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    const std::string changed = bodyOf(dock, "void PlaylistDock::onSourceChanged(int)");
    REQUIRE_FALSE(changed.empty());
    // saveSettings()/pendingSource_ are updated only for a deliberate user pick.
    const auto guard = changed.find("if (!refreshing_)");
    REQUIRE(guard != std::string::npos);
    CHECK(changed.find("saveSettings();", guard) != std::string::npos);
    CHECK(changed.find("pendingSource_ = name;", guard) != std::string::npos);

    // refreshSources() must set and clear the guard around its repopulation.
    const std::string refresh = bodyOf(dock, "void PlaylistDock::refreshSources()");
    CHECK(refresh.find("refreshing_ = true;") != std::string::npos);
    CHECK(refresh.find("refreshing_ = false;") != std::string::npos);

    // The persisted name is the remembered choice, not the combo's label, which
    // reads as the placeholder whenever the source is absent from the collection.
    const std::string save = bodyOf(dock, "void PlaylistDock::saveSettings() const");
    REQUIRE_FALSE(save.empty());
    CHECK(save.find("o[\"source\"] = pendingSource_;") != std::string::npos);
    CHECK(save.find("currentText()") == std::string::npos);
}

TEST_CASE("a user-configured media path is never cleared") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    const std::string body = bodyOf(dock, "void PlaylistDock::clearStalePluginFile()");
    REQUIRE_FALSE(body.empty());
    // Clearing is gated on the source's current file belonging to the playlist.
    CHECK(body.find("controller_.currentFile()") != std::string::npos);
    CHECK(body.find("playlist_.items()") != std::string::npos);
    CHECK(body.find("it.path == current") != std::string::npos);
    CHECK(body.find("controller_.clearFile();") != std::string::npos);

    // clearFile() must not be reachable from anywhere else in the dock.
    size_t calls = 0;
    for (size_t i = dock.find("controller_.clearFile()"); i != std::string::npos;
         i = dock.find("controller_.clearFile()", i + 1))
        ++calls;
    CHECK(calls == 1);
}

TEST_CASE("the collection swap remembers the bound source") {
    const std::string body = bodyOf(readSource("src/plugin/PlaylistDock.cpp"),
                                    "void PlaylistDock::releaseSource()");
    REQUIRE_FALSE(body.empty());
    CHECK(body.find("pendingSource_ = QString::fromStdString(controller_.boundName())") !=
          std::string::npos);
    CHECK(body.find("controller_.unbind();") != std::string::npos);
}

TEST_CASE("every locale defines the no-source-configured strings") {
    const std::vector<std::string> locales = {"de-DE", "en-US", "es-ES", "fr-FR", "it-IT",
                                              "ja-JP", "ko-KR", "pt-BR", "ru-RU", "zh-CN"};
    for (const auto& loc : locales) {
        const std::string ini = readSource(("data/locale/" + loc + ".ini").c_str());
        CAPTURE(loc);
        REQUIRE_FALSE(ini.empty());
        CHECK(ini.find("NoSourceConfigured=\"") != std::string::npos);
        CHECK(ini.find("Status.NoSourceConfigured=\"") != std::string::npos);
        CHECK(ini.find("Status.BoundTo=\"") != std::string::npos);
    }
}
