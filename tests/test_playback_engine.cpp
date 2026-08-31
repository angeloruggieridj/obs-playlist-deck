#include "doctest/doctest.h"
#include "PlaybackEngine.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace pld;

namespace {

// Records what the engine asked of a media source, and lets a test say what
// OBS would have answered. This is what makes the end-of-clip state machine
// testable at all: it used to be reachable only through a running OBS.
class FakeTransport : public IMediaTransport {
public:
    bool isBound = true;
    bool acceptsFiles = true;
    bool onAir = true;
    // A real media source reports that it started playing shortly after it is
    // handed a file. Modelling that is what keeps the tests honest: the guard in
    // mediaEnded() depends on it, and a fake that never started anything would
    // describe a source that does not exist. Tests about the guard itself turn
    // it off.
    bool autoStart = true;
    PlaybackEngine* engine = nullptr;

    std::vector<std::string> played;
    std::vector<std::string> staged;
    int stops = 0;

    bool bound() const override { return isBound; }
    bool playFile(const std::string& path) override {
        if (!acceptsFiles) return false;
        played.push_back(path);
        reportStarted();
        return true;
    }
    bool stageFile(const std::string& path) override {
        if (!acceptsFiles) return false;
        staged.push_back(path);
        reportStarted();
        return true;
    }
    void stop() override { ++stops; }
    bool inProgram() const override { return onAir; }

    void reportStarted() {
        if (autoStart && engine) engine->mediaStarted();
    }

    std::string lastPlayed() const { return played.empty() ? "" : played.back(); }
    std::string lastStaged() const { return staged.empty() ? "" : staged.back(); }
};

struct Fixture {
    Playlist playlist;
    FakeTransport transport;
    std::mt19937 rng{1234};
    PlaybackEngine engine{playlist, transport, rng};

    explicit Fixture(int count = 3) {
        transport.engine = &engine;
        for (int i = 0; i < count; ++i) {
            const std::string name = "clip" + std::to_string(i);
            playlist.add(PlaylistItem{"/media/" + name + ".mp4", name, -1});
        }
    }
};

} // namespace

TEST_CASE("play starts the requested item and makes it current") {
    Fixture f;
    const auto result = f.engine.play(1);
    CHECK(result.outcome == PlaybackOutcome::Started);
    CHECK(result.index == 1);
    CHECK(f.transport.lastPlayed() == "/media/clip1.mp4");
    CHECK(f.playlist.currentIndex() == 1);
}

TEST_CASE("play reports an unbound source instead of pretending") {
    Fixture f;
    f.transport.isBound = false;
    const auto result = f.engine.play(0);
    CHECK(result.outcome == PlaybackOutcome::NoSource);
    CHECK(f.transport.played.empty());
}

TEST_CASE("a transport that refuses the file is reported as a failure") {
    Fixture f;
    f.transport.acceptsFiles = false;
    CHECK(f.engine.play(0).outcome == PlaybackOutcome::Failed);
}

TEST_CASE("play on an index that does not exist does nothing") {
    Fixture f;
    CHECK(f.engine.play(9).outcome == PlaybackOutcome::Nothing);
    CHECK(f.transport.played.empty());
}

TEST_CASE("end of clip: play next advances, and stops at the end of the list") {
    Fixture f;
    f.engine.setMode(EndMode::PlayNext);
    f.engine.play(0);
    CHECK(f.engine.mediaEnded().index == 1);
    CHECK(f.engine.mediaEnded().index == 2);
    const auto atEnd = f.engine.mediaEnded();
    CHECK(atEnd.outcome == PlaybackOutcome::Stopped);
    CHECK(f.transport.stops == 1);
}

TEST_CASE("end of clip: loop wraps around") {
    Fixture f;
    f.engine.setMode(EndMode::Loop);
    f.engine.play(2);
    const auto wrapped = f.engine.mediaEnded();
    CHECK(wrapped.outcome == PlaybackOutcome::Started);
    CHECK(wrapped.index == 0);
    CHECK(f.transport.lastPlayed() == "/media/clip0.mp4");
}

TEST_CASE("end of clip: stop and repeat one") {
    Fixture f;
    f.engine.setMode(EndMode::StopAtEnd);
    f.engine.play(1);
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::Stopped);

    f.engine.setMode(EndMode::RepeatOne);
    f.engine.play(1);
    const auto again = f.engine.mediaEnded();
    CHECK(again.outcome == PlaybackOutcome::Started);
    CHECK(again.index == 1);
}

TEST_CASE("end of clip: shuffle plays every item before repeating one") {
    Fixture f(4);
    f.engine.setMode(EndMode::Shuffle);
    f.engine.play(0);
    std::vector<int> seen;
    for (int i = 0; i < 4; ++i) {
        const auto result = f.engine.mediaEnded();
        REQUIRE(result.outcome == PlaybackOutcome::Started);
        seen.push_back(result.index);
    }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    CHECK(seen.size() == 4); // a full bag, no repeats inside it
}

// The heart of "Load next (paused)": the clip is held until the bound source is
// off air, so its first frame never appears in Program.
TEST_CASE("load next: the clip waits while the source is on air") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(0);
    f.transport.onAir = true;

    const auto ended = f.engine.mediaEnded();
    CHECK(ended.outcome == PlaybackOutcome::StagePending);
    CHECK(ended.index == 1);
    CHECK(f.engine.pendingStage());
    CHECK(f.engine.stageIsWaiting());
    CHECK(f.transport.staged.empty()); // nothing loaded yet

    // A scene change that leaves the source on air changes nothing.
    CHECK(f.engine.programLayoutChanged().outcome == PlaybackOutcome::Nothing);
    CHECK(f.transport.staged.empty());

    // Off air: now it loads, paused.
    f.transport.onAir = false;
    const auto staged = f.engine.programLayoutChanged();
    CHECK(staged.outcome == PlaybackOutcome::Staged);
    CHECK(staged.index == 1);
    CHECK(f.transport.lastStaged() == "/media/clip1.mp4");
    CHECK_FALSE(f.engine.pendingStage());
    CHECK_FALSE(f.engine.stageIsWaiting());
}

TEST_CASE("load next: a source already off air stages immediately") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(0);
    f.transport.onAir = false;
    const auto ended = f.engine.mediaEnded();
    CHECK(ended.outcome == PlaybackOutcome::Staged);
    CHECK(f.transport.lastStaged() == "/media/clip1.mp4");
}

TEST_CASE("load next: the last clip stops rather than staging nothing") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(2);
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::Stopped);
    CHECK_FALSE(f.engine.pendingStage());
}

TEST_CASE("an explicit play cancels a staged clip") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(0);
    f.engine.mediaEnded();
    REQUIRE(f.engine.pendingStage());

    f.engine.play(2); // the operator picked something else
    CHECK_FALSE(f.engine.pendingStage());
    f.transport.onAir = false;
    CHECK(f.engine.programLayoutChanged().outcome == PlaybackOutcome::Nothing);
    CHECK(f.transport.staged.empty());
}

TEST_CASE("stageNow loads the pending clip without waiting") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(0);
    f.engine.mediaEnded();
    REQUIRE(f.engine.pendingStage());
    f.transport.onAir = true; // still live, but the operator asked

    const auto staged = f.engine.stageNow();
    CHECK(staged.outcome == PlaybackOutcome::Staged);
    CHECK(f.transport.lastStaged() == "/media/clip1.mp4");
    CHECK_FALSE(f.engine.pendingStage());
    CHECK(f.engine.stageNow().outcome == PlaybackOutcome::Nothing); // only once
}

TEST_CASE("a staged row that no longer exists is dropped, not loaded blindly") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(1);
    f.engine.mediaEnded();
    REQUIRE(f.engine.pendingStageRow() == 2);

    f.playlist.removeAt(2); // the queued clip was removed
    f.engine.playlistChanged();
    CHECK_FALSE(f.engine.pendingStage());
    f.transport.onAir = false;
    CHECK(f.engine.programLayoutChanged().outcome == PlaybackOutcome::Nothing);
    CHECK(f.transport.staged.empty());
}

TEST_CASE("Next and Previous move explicitly, and only Loop wraps") {
    Fixture f;
    f.engine.setMode(EndMode::PlayNext);
    f.engine.play(0);
    CHECK(f.engine.next().index == 1);
    CHECK(f.engine.prev().index == 0);
    CHECK(f.engine.prev().outcome == PlaybackOutcome::Nothing); // no wrap at the top

    f.engine.play(2);
    CHECK(f.engine.next().outcome == PlaybackOutcome::Nothing); // nor at the bottom

    f.engine.setMode(EndMode::Loop);
    f.engine.play(2);
    CHECK(f.engine.next().index == 0);
    CHECK(f.engine.prev().index == 2);
}

TEST_CASE("Next with nothing playing starts at the top") {
    Fixture f;
    CHECK(f.playlist.currentIndex() == -1);
    CHECK(f.engine.next().index == 0);
}

TEST_CASE("an empty playlist does nothing at all") {
    Fixture f(0);
    CHECK(f.engine.play(0).outcome == PlaybackOutcome::Nothing);
    CHECK(f.engine.next().outcome == PlaybackOutcome::Nothing);
    CHECK(f.engine.prev().outcome == PlaybackOutcome::Nothing);
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::Stopped);
    CHECK(f.engine.upNextIndex() == -1);
}

TEST_CASE("up next reports what will actually happen in each mode") {
    Fixture f;
    f.engine.setMode(EndMode::PlayNext);
    f.engine.play(0);
    CHECK(f.engine.upNextIndex() == 1);
    f.engine.play(2);
    CHECK(f.engine.upNextIndex() == -1); // nothing follows the last item

    f.engine.setMode(EndMode::Loop);
    CHECK(f.engine.upNextIndex() == 0);

    f.engine.setMode(EndMode::RepeatOne);
    CHECK(f.engine.upNextIndex() == 2);

    f.engine.setMode(EndMode::StopAtEnd);
    CHECK(f.engine.upNextIndex() == -1);
}

TEST_CASE("up next names the staged clip while one is pending") {
    Fixture f;
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(0);
    f.engine.mediaEnded();
    CHECK(f.engine.upNextIndex() == 1);
}

TEST_CASE("changing mode drops a shuffle bag drawn for the previous one") {
    Fixture f(6);
    f.engine.setMode(EndMode::Shuffle);
    f.engine.play(0);
    f.engine.mediaEnded();
    f.engine.setMode(EndMode::PlayNext);
    // The next item is now the sequential one, not whatever the bag held.
    const int current = f.playlist.currentIndex();
    const auto result = f.engine.mediaEnded();
    CHECK(result.index == current + 1);
}

// Regression, 1.3.1. Handing the source a new file makes it report that the
// *outgoing* media ended, on top of the end that started the sequence. In
// "Load next (paused)" that second end arrived when the deck was already off
// air, so it was acted on at once: the playlist advanced by two, item 1 was
// replaced by item 2 before anyone saw it, and the list highlighted the wrong
// clip. The exact sequence the user hit:
TEST_CASE("a spurious end from the outgoing clip does not advance the playlist twice") {
    Fixture f(4);
    f.transport.autoStart = false; // the handshake is what this case is about
    f.engine.setMode(EndMode::LoadNext);
    f.engine.play(0);
    f.engine.mediaStarted(); // clip 0 is really playing
    f.transport.onAir = true;

    // Clip 0 ends while the source is on air: item 1 is queued, nothing loaded.
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::StagePending);
    CHECK(f.engine.pendingStageRow() == 1);

    // Program -> Preview: item 1 is loaded, paused.
    f.transport.onAir = false;
    const auto staged = f.engine.programLayoutChanged();
    CHECK(staged.outcome == PlaybackOutcome::Staged);
    CHECK(staged.index == 1);
    CHECK(f.transport.lastStaged() == "/media/clip1.mp4");

    // The source now reports that the clip it was told to drop has ended. It is
    // not clip 1 - clip 1 has not started yet - so nothing happens.
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::Nothing);
    CHECK(f.playlist.currentIndex() == 1);
    CHECK(f.transport.staged.size() == 1);

    // And once clip 1 really plays, its own end advances by exactly one.
    f.engine.mediaStarted();
    f.transport.onAir = true;
    const auto next = f.engine.mediaEnded();
    CHECK(next.outcome == PlaybackOutcome::StagePending);
    CHECK(next.index == 2);
}

TEST_CASE("the same guard protects a plain auto-advance") {
    Fixture f(4);
    f.transport.autoStart = false;
    f.engine.setMode(EndMode::PlayNext);
    f.engine.play(0);
    f.engine.mediaStarted();

    CHECK(f.engine.mediaEnded().index == 1); // clip 0 ended: play clip 1
    // The reload makes the source report the outgoing clip's end.
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::Nothing);
    CHECK(f.playlist.currentIndex() == 1);
    CHECK(f.transport.played.size() == 2);

    f.engine.mediaStarted();
    CHECK(f.engine.mediaEnded().index == 2); // and the real end still advances
}

TEST_CASE("a source that never reports a start costs one advance, not the deck") {
    Fixture f(4);
    f.transport.autoStart = false;
    f.engine.setMode(EndMode::PlayNext);
    f.engine.play(0);
    // No mediaStarted() at all: the first end is treated as the outgoing clip's.
    CHECK(f.engine.mediaEnded().outcome == PlaybackOutcome::Nothing);
    // The next one is honoured, so auto-advance cannot wedge for good.
    CHECK(f.engine.mediaEnded().index == 1);
}

TEST_CASE("an explicit request is never blocked by the guard") {
    Fixture f(4);
    f.transport.autoStart = false;
    f.engine.play(0); // no start reported
    CHECK(f.engine.next().index == 1);
    CHECK(f.engine.prev().index == 0);
    CHECK(f.engine.play(3).index == 3);
}
