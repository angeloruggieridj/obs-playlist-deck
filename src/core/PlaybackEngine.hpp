// SPDX-License-Identifier: MIT
#pragma once
#include <random>
#include <string>
#include "EndMode.hpp"
#include "Playlist.hpp"
#include "Shuffle.hpp"
#include "Staging.hpp"

namespace pld {

// What the engine needs from a media source, and nothing more.
//
// The real implementation is the OBS controller; the tests supply a fake that
// records what it was asked to do. That is the point of the interface: the
// end-of-clip state machine used to be reachable only through a running OBS,
// so the one part of the plugin that decides what goes on air was also the
// part that could not be tested.
class IMediaTransport {
public:
    virtual ~IMediaTransport() = default;

    virtual bool bound() const = 0;
    // Load the file and start playing it.
    virtual bool playFile(const std::string& path) = 0;
    // Load the file and hold it paused on its first frame.
    virtual bool stageFile(const std::string& path) = 0;
    virtual void stop() = 0;
    // Whether the bound source is part of the scene currently on air.
    virtual bool inProgram() const = 0;
};

enum class PlaybackOutcome {
    Nothing,      // the request had nothing to do
    Started,      // `index` is playing
    Staged,       // `index` is loaded and paused, off air
    StagePending, // `index` will be staged once the source leaves Program
    Stopped,      // playback stopped
    NoSource,     // nothing is bound
    Failed,       // the transport refused the file
};

struct PlaybackResult {
    PlaybackOutcome outcome = PlaybackOutcome::Nothing;
    int index = -1;
};

// Owns "what plays next and when": the end-of-clip modes, the shuffle bag and
// the staged-clip rule. It drives a transport and mutates the playlist's
// current index; everything visual stays with the dock.
class PlaybackEngine {
public:
    PlaybackEngine(Playlist& playlist, IMediaTransport& transport, std::mt19937& rng);

    void setMode(EndMode mode);
    EndMode mode() const { return mode_; }

    // Explicit requests.
    PlaybackResult play(int index);
    PlaybackResult next();
    PlaybackResult prev();

    // The bound source reported that the clip finished.
    PlaybackResult mediaEnded();

    // Program or preview changed: a clip staged behind "Load next (paused)" is
    // loaded as soon as the source is no longer on air.
    PlaybackResult programLayoutChanged();

    // Load a pending staged clip now, whatever the source's state — the manual
    // escape hatch for an operator who does not want to wait.
    PlaybackResult stageNow();

    bool pendingStage() const { return pendingStage_; }
    int pendingStageRow() const { return pendingStageRow_; }
    // True while the staged clip is waiting on a source that is still on air,
    // which is the only time it is worth telling the operator about.
    bool stageIsWaiting() const;

    // The item that would play next, for the "then:" line. -1 when unknown.
    int upNextIndex() const;

    // The playlist changed under us: the shuffle bag's indices no longer mean
    // anything, and a pending stage may point at an item that is gone.
    void playlistChanged();

private:
    PlaybackResult startAt(int index);
    PlaybackResult stageAt(int index);
    int drawShuffle();

    Playlist& playlist_;
    IMediaTransport& transport_;
    std::mt19937& rng_;
    EndMode mode_ = EndMode::PlayNext;
    ShuffleQueue shuffle_;
    bool pendingStage_ = false;
    int pendingStageRow_ = -1;
};

} // namespace pld
