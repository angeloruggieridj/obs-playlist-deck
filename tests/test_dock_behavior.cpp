#include "doctest/doctest.h"

#include <fstream>
#include <iterator>
#include <string>

// Source-inspection guards for the 1.3 fixes. They pin decisions that cannot be
// exercised without a running OBS and a Qt event loop; the behaviour they
// protect is described in each case, so a future change either keeps the
// decision or has to argue with the comment.
namespace {
std::string readSource(const char* relativePath) {
    std::ifstream input(std::string(PLD_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

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
} // namespace

// F-1. OBS's VLC source has no local_file setting: it reads a "playlist" array
// of {value: path} objects. Writing local_file on it is accepted silently and
// changes nothing, so binding a VLC source produced a deck whose every button
// appeared to work and never altered what was playing.
TEST_CASE("the VLC source is driven through its playlist array, not local_file") {
    const std::string controller = readSource("src/plugin/MediaSourceController.cpp");
    const std::string body = bodyOf(controller, "bool MediaSourceController::writeFile");
    REQUIRE_FALSE(body.empty());

    CHECK(body.find("SourceKind::Vlc") != std::string::npos);
    CHECK(body.find("obs_data_array_create()") != std::string::npos);
    CHECK(body.find("obs_data_set_string(entry, \"value\"") != std::string::npos);
    CHECK(body.find("obs_data_set_array(settings, \"playlist\"") != std::string::npos);
    // Arrays and entries are refcounted like everything else in obs_data.
    CHECK(body.find("obs_data_array_release(arr)") != std::string::npos);
    CHECK(body.find("obs_data_release(entry)") != std::string::npos);
    // A looping VLC playlist never reaches its end, so media_ended would never
    // fire and auto-advance would be dead for VLC sources.
    CHECK(body.find("obs_data_set_bool(settings, \"loop\", false)") != std::string::npos);
    // The ffmpeg branch keeps its own settings.
    CHECK(body.find("local_file") != std::string::npos);
    CHECK(body.find("is_local_file") != std::string::npos);

    // Every write goes through that one function, so the two shapes cannot
    // drift apart again.
    CHECK(controller.find("obs_data_set_string(settings, \"local_file\"") != std::string::npos);
    size_t writes = 0;
    for (size_t i = controller.find("obs_source_update("); i != std::string::npos;
         i = controller.find("obs_source_update(", i + 1))
        ++writes;
    CHECK(writes == 1);

    // Reading the file back has to understand both shapes too, or the
    // stale-file cleanup at startup has nothing to compare against.
    const std::string current = bodyOf(controller, "std::string MediaSourceController::currentFile");
    CHECK(current.find("obs_data_get_array(settings, \"playlist\")") != std::string::npos);
}

// F-3. The vendor callbacks run on the websocket thread. GetStatus used to read
// the playlist model from there while the UI thread was mutating the same
// std::vector: a read landing inside an erase is undefined behaviour.
TEST_CASE("remote status is read from a published snapshot, not the live model") {
    const std::string main = readSource("src/plugin/plugin-main.cpp");
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string hpp = readSource("src/plugin/PlaylistDock.hpp");

    // No direct model access from the websocket thread.
    CHECK(main.find("g_dock->itemCount()") == std::string::npos);
    CHECK(main.find("g_dock->currentIndex()") == std::string::npos);
    CHECK(main.find("g_dock->status()") != std::string::npos);

    // The snapshot is published and read under a mutex.
    CHECK(hpp.find("std::mutex snapshotMutex_") != std::string::npos);
    const std::string status = bodyOf(dock, "DeckStatus PlaylistDock::status() const");
    REQUIRE_FALSE(status.empty());
    CHECK(status.find("std::lock_guard<std::mutex>") != std::string::npos);
    CHECK(bodyOf(dock, "void PlaylistDock::snapshotStatus()").find("std::lock_guard") !=
          std::string::npos);

    // Everything that mutates still crosses to the UI thread.
    CHECK(main.find("Qt::QueuedConnection") != std::string::npos);
}

// F-4. A truncate-then-write leaves a half-written file behind if anything
// interrupts it, and truncated JSON does not parse: the whole saved playlist is
// gone. And the session was rewritten on every rebuild — once per added file,
// per probed duration, per reorder.
TEST_CASE("state is written atomically and the session write is debounced") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");

    CHECK(dock.find("QSaveFile") != std::string::npos);
    CHECK(dock.find("f.commit()") != std::string::npos);
    // The only truncating open left in the file is QSaveFile's own, inside the
    // helper: no caller writes over a file in place any more.
    size_t truncatingOpens = 0;
    for (size_t i = dock.find("QIODevice::WriteOnly"); i != std::string::npos;
         i = dock.find("QIODevice::WriteOnly", i + 1))
        ++truncatingOpens;
    CHECK(truncatingOpens == 1);
    CHECK(bodyOf(dock, "bool writeAtomically").find("QIODevice::WriteOnly") != std::string::npos);
    CHECK(bodyOf(dock, "void PlaylistDock::saveSettings() const").find("writeAtomically") !=
          std::string::npos);
    CHECK(bodyOf(dock, "void PlaylistDock::saveSession() const").find("writeAtomically") !=
          std::string::npos);
    // The user's own playlist file gets the same treatment.
    CHECK(bodyOf(dock, "void PlaylistDock::onSavePlaylist()").find("writeAtomically") !=
          std::string::npos);

    // rebuildList() schedules a save instead of performing one.
    const std::string rebuild = bodyOf(dock, "void PlaylistDock::rebuildList()");
    REQUIRE_FALSE(rebuild.empty());
    CHECK(rebuild.find("scheduleSessionSave()") != std::string::npos);
    CHECK(rebuild.find("saveSession()") == std::string::npos);
    // And a pending debounce is flushed on the way out.
    CHECK(bodyOf(dock, "void PlaylistDock::shutdown()").find("saveSession()") !=
          std::string::npos);

    // M-3: the schema version was written from the start but never read.
    CHECK(bodyOf(dock, "void PlaylistDock::loadSession()").find("\"version\"") !=
          std::string::npos);
    // F-26: the playlist came back but the label claimed nothing was loaded.
    CHECK(bodyOf(dock, "void PlaylistDock::saveSession() const").find("loadedPath") !=
          std::string::npos);
}

// F-5. rebuildList() stat()ed every path on every call — that is every add,
// remove, move, reorder, probe result and language change. On a network share
// it is a visible freeze on every click.
TEST_CASE("existence checks are cached and done off the UI thread") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string rebuild = bodyOf(dock, "void PlaylistDock::rebuildList()");
    REQUIRE_FALSE(rebuild.empty());

    CHECK(rebuild.find("QFileInfo::exists") == std::string::npos);
    CHECK(rebuild.find("existsCache_") != std::string::npos);
    // The scanner thread is where the filesystem is touched.
    const std::string scanner = readSource("src/plugin/MediaScanner.cpp");
    CHECK(scanner.find("QFileInfo::exists") != std::string::npos);
    // M-1: results arrive in batches, so a large drop does not rebuild the list
    // once per file.
    CHECK(scanner.find("batchReady") != std::string::npos);
    CHECK(scanner.find("kBatchSize") != std::string::npos);
    // A batch about a playlist that has since been replaced is discarded.
    CHECK(scanner.find("generation") != std::string::npos);
}

// F-6. "deactivate" fires only when a source is in no active scene at all, and
// in studio mode a source in preview still counts as active — so the ordinary
// program -> preview transition raised nothing and the staged clip was never
// loaded.
TEST_CASE("a staged clip is triggered by scene changes, not by deactivate") {
    const std::string main = readSource("src/plugin/plugin-main.cpp");
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string controller = readSource("src/plugin/MediaSourceController.cpp");

    CHECK(main.find("OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED") != std::string::npos);
    CHECK(main.find("OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED") != std::string::npos);
    CHECK(main.find("OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED") != std::string::npos);
    CHECK(main.find("programLayoutChanged()") != std::string::npos);

    // The decision itself is the pure rule in the core, so it is unit-tested.
    CHECK(dock.find("pld::shouldStageNow") != std::string::npos);
    // Which needs a real answer to "is the bound source on air".
    CHECK(controller.find("obs_frontend_get_current_scene()") != std::string::npos);
    CHECK(controller.find("obs_scene_find_source_recursive") != std::string::npos);
    // deactivate stays connected, as the fallback for a source removed from
    // every scene.
    CHECK(controller.find("\"deactivate\"") != std::string::npos);
}

// F-7. The duration was read 700 ms after playIndex(), on a fixed timer with no
// idea which item it belonged to: pressing Next quickly wrote the new clip's
// duration onto the previous item.
TEST_CASE("a captured duration belongs to the item it was scheduled for") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string capture = bodyOf(dock, "void PlaylistDock::captureDuration()");
    REQUIRE_FALSE(capture.empty());

    CHECK(capture.find("playlist_.currentIndex() != captureRow_") != std::string::npos);
    CHECK(capture.find("capturePath_") != std::string::npos);
    // The trigger is the source reporting that it started, not a guessed delay.
    CHECK(dock.find("void PlaylistDock::onMediaStarted()") != std::string::npos);
    CHECK(readSource("src/plugin/MediaSourceController.cpp").find("\"media_started\"") !=
          std::string::npos);
    CHECK(dock.find("QTimer::singleShot(700") == std::string::npos);
}

// F-8. The plugin ships ten languages while about fourteen user-visible strings
// were hardcoded English, which is exactly the mix that reads as unfinished.
TEST_CASE("no user-visible string is hardcoded English") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    for (const char* literal :
         {"\"Playing: %1\"", "\"Failed to set media source.\"", "\"Loaded (paused): %1\"",
          "\"Added %1 file(s).\"", "\"Cannot open playlist file.\"", "\"Invalid JSON playlist.\"",
          "\"Cannot write playlist file.\"", "\"Saved: %1\"", "\"Opened: %1\"",
          "\"Add media files\"", "\"Save playlist\"", "\"Open playlist\""}) {
        CAPTURE(literal);
        CHECK(dock.find(literal) == std::string::npos);
    }
    // Including the file dialog titles, which are as visible as any label.
    CHECK(dock.find("T(\"Dlg.AddFiles\")") != std::string::npos);
    CHECK(dock.find("T(\"Dlg.SavePlaylist\")") != std::string::npos);
    CHECK(dock.find("T(\"Dlg.OpenPlaylist\")") != std::string::npos);
}

// F-10. Four small structs were handed to obs_hotkey_register_frontend with a
// bare `new` and never freed. They are owned now, and released only after the
// hotkeys are unregistered — while a callback could still arrive, freeing them
// would be worse than the leak.
TEST_CASE("hotkey targets are owned, and outlive their hotkeys") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    CHECK(dock.find("new HotkeyTarget{") == std::string::npos);
    CHECK(dock.find("std::make_unique<HotkeyTarget>()") != std::string::npos);
    const std::string unreg = bodyOf(dock, "void PlaylistDock::unregisterHotkeys()");
    REQUIRE_FALSE(unreg.empty());
    const auto unregisterAt = unreg.find("obs_hotkey_unregister(id)");
    const auto clearAt = unreg.find("hotkeyTargets_.clear()");
    REQUIRE(unregisterAt != std::string::npos);
    REQUIRE(clearAt != std::string::npos);
    CHECK(unregisterAt < clearAt); // order matters, and this is why
}

// F-17. Every button carried an accessibleName and Qt::NoFocus at the same
// time: named for a screen reader, unreachable by the keyboard that drives one.
TEST_CASE("the dock is reachable from the keyboard") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string list = readSource("src/plugin/PlaylistListWidget.cpp");

    CHECK(dock.find("setFocusPolicy(Qt::NoFocus)") == std::string::npos);
    CHECK(dock.find("setAccessibleName") != std::string::npos);
    // Return plays, Delete removes, F2 renames, Ctrl+F focuses the filter.
    CHECK(list.find("Qt::Key_Return") != std::string::npos);
    CHECK(list.find("Qt::Key_Delete") != std::string::npos);
    CHECK(list.find("Qt::Key_F2") != std::string::npos);
    CHECK(dock.find("QKeySequence::Find") != std::string::npos);
    CHECK(dock.find("QKeySequence::Undo") != std::string::npos);
}

// F-18. Icons were tinted once, from the palette in force when the dock was
// built. OBS 31 switches theme while running, and the icons stayed dark on dark.
TEST_CASE("icons follow a theme switch") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    CHECK(dock.find("void PlaylistDock::changeEvent(QEvent* event)") != std::string::npos);
    CHECK(dock.find("QEvent::PaletteChange") != std::string::npos);
    CHECK(dock.find("retintIcons()") != std::string::npos);
    // The colour tokens are derived from the palette rather than hardcoded.
    CHECK(dock.find("#e06c75") == std::string::npos);
    CHECK(readSource("src/plugin/DeckStyle.hpp").find("QPalette& pal") != std::string::npos);
}

// F-19. The 500 ms tick ran for the whole OBS session even with the dock
// closed, polling the source and repainting widgets nobody could see.
TEST_CASE("the refresh timer stops while the dock is hidden") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    CHECK(bodyOf(dock, "void PlaylistDock::hideEvent").find("uiTimer_->stop()") !=
          std::string::npos);
    CHECK(bodyOf(dock, "void PlaylistDock::showEvent").find("uiTimer_->start()") !=
          std::string::npos);
}

// F-23. Load names any path on the machine and the reply is rendered in the
// dock; a 500 MB "playlist" read whole would freeze OBS.
TEST_CASE("a playlist file is size-capped before it is read") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    const std::string load = bodyOf(dock, "bool PlaylistDock::loadPlaylistFile");
    REQUIRE_FALSE(load.empty());
    CHECK(load.find("kMaxPlaylistBytes") != std::string::npos);
    CHECK(load.find("Status.TooLarge") != std::string::npos);
}

// The remote API gained fields and requests without changing the two it always
// had, so existing scripts keep working.
TEST_CASE("the vendor API stays backward compatible") {
    const std::string main = readSource("src/plugin/plugin-main.cpp");
    for (const char* request : {"\"Next\"", "\"Previous\"", "\"Stop\"", "\"PlayPause\"",
                                "\"PlayIndex\"", "\"Load\"", "\"GetStatus\""}) {
        CAPTURE(request);
        CHECK(main.find(request) != std::string::npos);
    }
    // v1 response fields.
    CHECK(main.find("obs_data_set_int(resp, \"count\"") != std::string::npos);
    CHECK(main.find("obs_data_set_int(resp, \"currentIndex\"") != std::string::npos);
    // v2 additions.
    for (const char* request : {"\"GetItems\"", "\"SetMode\"", "\"Seek\"", "\"AddPaths\"",
                                "\"Clear\""}) {
        CAPTURE(request);
        CHECK(main.find(request) != std::string::npos);
    }
    // Events: the header has always exposed the emit call; nothing used it.
    CHECK(main.find("obs_websocket_vendor_emit_event") != std::string::npos);
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    CHECK(dock.find("\"item-started\"") != std::string::npos);
    CHECK(dock.find("\"playback-state\"") != std::string::npos);
    CHECK(dock.find("\"playlist-completed\"") != std::string::npos);
}

// F-15. Four icons sat in the .qrc unused, which reads as abandoned work. They
// are used now: pencil renames, trash clears, download adds a folder, upload
// exports.
TEST_CASE("no icon ships without being used") {
    const std::string qrc = readSource("resources/icons.qrc");
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    REQUIRE_FALSE(qrc.empty());
    for (const char* icon : {"plus", "minus", "chevron-up", "chevron-down", "x", "play", "pause",
                             "stop", "skip-back", "skip-forward", "save", "folder-open", "pencil",
                             "trash", "download", "upload", "refresh", "settings", "search",
                             "music"}) {
        CAPTURE(icon);
        const std::string file = std::string("icons/") + icon + ".svg";
        CHECK(qrc.find("<file>" + file + "</file>") != std::string::npos);
        CHECK(dock.find(":/" + file) != std::string::npos);
    }
}
