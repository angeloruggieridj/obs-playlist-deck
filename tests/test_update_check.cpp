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
    const std::string checker = readSource("src/plugin/UpdateChecker.cpp");
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string cmake = readSource("src/plugin/CMakeLists.txt");
    REQUIRE_FALSE(checker.empty());

    for (const std::string& source : {checker, dock}) {
        CHECK(source.find("QNetworkAccessManager") == std::string::npos);
        CHECK(source.find("QNetworkReply") == std::string::npos);
        CHECK(source.find("QNetworkRequest") == std::string::npos);
    }
    // The Qt Network component must not be linked back in by accident.
    CHECK(cmake.find("Qt6::Network") == std::string::npos);
    CHECK(cmake.find("CURL::libcurl") != std::string::npos);
}

TEST_CASE("the update check uses libcurl off the UI thread") {
    const std::string checker = readSource("src/plugin/UpdateChecker.cpp");

    CHECK(checker.find("curl_easy_perform") != std::string::npos);
    // Blocking network I/O must not run on the Qt main thread.
    CHECK(checker.find("moveToThread") != std::string::npos);
    CHECK(checker.find("Qt::QueuedConnection") != std::string::npos);
    // Every handle acquired has to be released.
    CHECK(checker.find("curl_easy_cleanup") != std::string::npos);
    CHECK(checker.find("curl_slist_free_all") != std::string::npos);
    // A rate-limited 403 carries a JSON body without tag_name; treat non-200 as
    // an error rather than silently as "no update available".
    CHECK(checker.find("CURLINFO_RESPONSE_CODE") != std::string::npos);
}

// S-1: the request is made from a worker thread inside someone else's
// application, and it follows redirects. Each of these options closes a hole
// that opens up because of one of those two facts.
TEST_CASE("the curl handle is configured for safe use inside OBS") {
    const std::string checker = readSource("src/plugin/UpdateChecker.cpp");

    // Without NOSIGNAL libcurl resolves DNS timeouts with SIGALRM and
    // siglongjmp, which is undefined behaviour off the main thread.
    CHECK(checker.find("CURLOPT_NOSIGNAL") != std::string::npos);
    // FOLLOWLOCATION on its own follows a redirect anywhere, including off
    // HTTPS and any number of times.
    CHECK(checker.find("CURLOPT_MAXREDIRS") != std::string::npos);
    CHECK(checker.find("CURLOPT_REDIR_PROTOCOLS") != std::string::npos);
    // TIMEOUT alone lets a black-holed connection hold the thread for its full
    // duration before anything happens at all.
    CHECK(checker.find("CURLOPT_CONNECTTIMEOUT") != std::string::npos);
    // And the reply body is bounded: the write callback returns short to abort.
    CHECK(checker.find("kMaxBodyBytes") != std::string::npos);
}

// F-2: a detached thread has no owner, cannot be cancelled, and can still be
// posting results into a Qt application that is being torn down.
TEST_CASE("no plugin code detaches a thread") {
    for (const char* file :
         {"src/plugin/UpdateChecker.cpp", "src/plugin/PlaylistDock.cpp",
          "src/plugin/MediaScanner.cpp", "src/plugin/plugin-main.cpp"}) {
        const std::string source = readSource(file);
        CAPTURE(file);
        REQUIRE_FALSE(source.empty());
        CHECK(source.find(".detach()") == std::string::npos);
        CHECK(source.find("std::thread(") == std::string::npos);
    }
    // The threads that do exist are owned and waited for.
    const std::string checker = readSource("src/plugin/UpdateChecker.cpp");
    CHECK(checker.find("thread_->quit()") != std::string::npos);
    CHECK(checker.find("thread_->wait(") != std::string::npos);
    const std::string scanner = readSource("src/plugin/MediaScanner.cpp");
    CHECK(scanner.find("thread_->quit()") != std::string::npos);
    CHECK(scanner.find("thread_->wait(") != std::string::npos);
    // And the dock stops them while there is still an event loop to do it with.
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    CHECK(dock.find("scanner_->shutdown()") != std::string::npos);
    CHECK(dock.find("updateChecker_->shutdown()") != std::string::npos);
}

TEST_CASE("a second update check cannot start while one is running") {
    const std::string checker = readSource("src/plugin/UpdateChecker.cpp");
    // Clicking the button five times used to start five threads.
    CHECK(checker.find("if (stopped_ || busy_) return false;") != std::string::npos);
}

TEST_CASE("update check failures are visible, not only logged") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    // A manual check reports its outcome in the status line.
    CHECK(dock.find("Status.UpdateCheckFailed") != std::string::npos);
    CHECK(dock.find("Status.UpToDate") != std::string::npos);
    CHECK(dock.find("Status.UpdateAvailable") != std::string::npos);
    // And there is a way to ask again without restarting OBS.
    CHECK(dock.find("Btn.CheckUpdates") != std::string::npos);
    CHECK(dock.find("updateChecker_->check(true)") != std::string::npos);
    // The automatic startup check stays silent on failure.
    CHECK(dock.find("updateChecker_->check(false)") != std::string::npos);
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
