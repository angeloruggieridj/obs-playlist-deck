#include "doctest/doctest.h"

#include "Version.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string readSource(const char* relativePath) {
    std::ifstream input(std::string(PLD_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

// Regression: the update check used QNetworkAccessManager, which needs a Qt TLS
// backend plugin to speak HTTPS. OBS ships none on Windows, so every check died
// with "TLS initialization failed" and the plugin never reported a new release.
// libcurl is bundled with OBS on all three platforms and carries its own TLS.
TEST_CASE("the update check does not depend on Qt Network") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string hpp = readSource("src/plugin/PlaylistDock.hpp");
    const std::string cmake = readSource("src/plugin/CMakeLists.txt");
    REQUIRE_FALSE(dock.empty());

    CHECK(dock.find("QNetworkAccessManager") == std::string::npos);
    CHECK(dock.find("QNetworkReply") == std::string::npos);
    CHECK(dock.find("QNetworkRequest") == std::string::npos);
    CHECK(hpp.find("QNetworkAccessManager") == std::string::npos);
    // The Qt Network component must not be linked back in by accident.
    CHECK(cmake.find("Qt6::Network") == std::string::npos);
    CHECK(cmake.find("CURL::libcurl") != std::string::npos);
}

TEST_CASE("the update check uses libcurl off the UI thread") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    CHECK(dock.find("curl_easy_perform") != std::string::npos);
    // Blocking network I/O must not run on the Qt main thread.
    CHECK(dock.find("std::thread") != std::string::npos);
    CHECK(dock.find("Qt::QueuedConnection") != std::string::npos);
    // Every handle acquired has to be released.
    CHECK(dock.find("curl_easy_cleanup") != std::string::npos);
    CHECK(dock.find("curl_slist_free_all") != std::string::npos);
    // A rate-limited 403 carries a JSON body without tag_name; treat non-200 as
    // an error rather than silently as "no update available".
    CHECK(dock.find("CURLINFO_RESPONSE_CODE") != std::string::npos);
}

TEST_CASE("update check failures are visible, not only logged") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    // A manual check reports its outcome in the status line.
    CHECK(dock.find("Status.UpdateCheckFailed") != std::string::npos);
    CHECK(dock.find("Status.UpToDate") != std::string::npos);
    CHECK(dock.find("Status.UpdateAvailable") != std::string::npos);
    // And there is a way to ask again without restarting OBS.
    CHECK(dock.find("Btn.CheckUpdates") != std::string::npos);
    CHECK(dock.find("checkForUpdate(true)") != std::string::npos);
    // The automatic startup check stays silent on failure.
    CHECK(dock.find("checkForUpdate(false)") != std::string::npos);
}

TEST_CASE("every locale defines the update-check strings") {
    const std::vector<std::string> locales = {"de-DE", "en-US", "es-ES", "fr-FR", "it-IT",
                                              "ja-JP", "ko-KR", "pt-BR", "ru-RU", "zh-CN"};
    for (const auto& loc : locales) {
        const std::string ini = readSource(("data/locale/" + loc + ".ini").c_str());
        CAPTURE(loc);
        REQUIRE_FALSE(ini.empty());
        CHECK(ini.find("Btn.CheckUpdates=\"") != std::string::npos);
        CHECK(ini.find("Status.UpdateChecking=\"") != std::string::npos);
        CHECK(ini.find("Status.UpdateCheckFailed=\"") != std::string::npos);
        CHECK(ini.find("Status.UpdateAvailable=\"") != std::string::npos);
        CHECK(ini.find("Status.UpToDate=\"") != std::string::npos);
    }
}

// The comparison that decides whether the notification appears at all.
TEST_CASE("version comparison drives the notification correctly") {
    CHECK(pld::isNewerVersion("v1.2.3", "1.2.2"));
    CHECK(pld::isNewerVersion("1.2.3", "1.2.2"));
    CHECK(pld::isNewerVersion("v1.3.0", "1.2.9"));
    CHECK(pld::isNewerVersion("v2.0.0", "1.9.9"));
    CHECK_FALSE(pld::isNewerVersion("v1.2.2", "1.2.2"));
    CHECK_FALSE(pld::isNewerVersion("v1.2.1", "1.2.2"));
}
