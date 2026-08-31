// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <obs.h>
#include "PlaybackEngine.hpp"
#include "SourceKind.hpp"

// Wraps the libobs calls needed to drive an existing media source
// (ffmpeg_source / vlc_source) from the playlist. Keeps a strong reference to
// the bound source and subscribes to its media signals for auto-advance.
//
// The two source types take the file to play through completely different
// settings, so everything that writes one goes through setFile() and the
// SourceKind recorded at bind() time — see the comment there.
//
// It implements pld::IMediaTransport, which is the whole of what the playback
// engine is allowed to know about OBS: five calls, so the engine can be tested
// against a fake instead of a running application.
class MediaSourceController : public pld::IMediaTransport {
public:
    ~MediaSourceController() override;

    static std::vector<std::string> listMediaSources();

    // Sources that hold a list of media paths in their settings — a VLC source,
    // OBS's own playlist source, whatever a plugin adds. Reading one is
    // generic; driving one is not, which is why these are offered for import
    // rather than added to the bindable list. Writing settings this plugin has
    // never seen is the mistake that left VLC support inert for three releases.
    static std::vector<std::string> listPlaylistCapableSources();
    // The media paths a source holds, in order. Empty when it holds none.
    static std::vector<std::string> readSourcePlaylist(const std::string& sourceName);

    void bind(const std::string& sourceName);
    void unbind();
    bool isBound() const { return source_ != nullptr; }
    std::string boundName() const { return boundName_; }
    pld::SourceKind kind() const { return kind_; }

    // pld::IMediaTransport.
    bool bound() const override { return isBound(); }
    bool playFile(const std::string& path) override { return setFileAndRestart(path); }
    bool stageFile(const std::string& path) override { return setFileLoadOnly(path); }
    void stop() override;
    bool inProgram() const override { return isInProgram(); }

    bool setFileAndRestart(const std::string& path);
    bool setFileLoadOnly(const std::string& path); // set file, then pause on first frame
    bool clearFile();                              // clear the file + stop (no stale playback)
    std::string currentFile() const;               // file currently set on the source
    long long currentDurationMs() const;           // -1 if unknown / not bound
    long long currentTimeMs() const;               // playback position, -1 if not bound
    bool isPlaying() const;
    bool isPaused() const;

    // Audio. The mute lives on the OBS source, shared with the audio mixer:
    // the deck reads and writes it, and follows it when it is changed from
    // elsewhere, rather than keeping a second opinion of its own.
    bool isMuted() const;
    void setMuted(bool muted);
    void toggleMute();
    void play();
    void pause();
    void togglePlayPause();
    void restart();
    bool seekMs(long long ms);

    // Whether the bound source is part of the scene currently on air. This is
    // what "off air" means for the staged-clip rule (see Staging.hpp): the
    // source's own "deactivate" signal does not answer that question, because a
    // source sitting in the studio-mode preview still counts as active.
    bool isInProgram() const;

    void setOnMediaEnded(std::function<void()> cb) { onEnded_ = std::move(cb); }
    // Fires once the source has actually opened a file and started playing it.
    // Duration is only readable from that point on, which is why the dock waits
    // for this instead of guessing with a timer.
    void setOnMediaStarted(std::function<void()> cb) { onStarted_ = std::move(cb); }
    // Fires when the source is in no active scene at all. Kept as a fallback
    // for the staged-clip rule (a source removed from every scene never raises
    // a scene change), not as its primary trigger.
    void setOnDeactivated(std::function<void()> cb) { onDeactivated_ = std::move(cb); }
    // Fires when the source's mute is changed by anyone — including the OBS
    // mixer, which is why the dock cannot simply remember what it last set.
    void setOnMuteChanged(std::function<void(bool)> cb) { onMuteChanged_ = std::move(cb); }

private:
    static void mediaEndedThunk(void* data, calldata_t* cd);
    static void mediaStartedThunk(void* data, calldata_t* cd);
    static void deactivateThunk(void* data, calldata_t* cd);
    static void muteThunk(void* data, calldata_t* cd);

    // Writes the path into whichever setting the bound source actually reads.
    bool writeFile(const std::string& path);

    obs_source_t* source_ = nullptr; // strong ref while bound
    std::string boundName_;
    pld::SourceKind kind_ = pld::SourceKind::Unsupported;
    std::function<void()> onEnded_;
    std::function<void()> onStarted_;
    std::function<void()> onDeactivated_;
    std::function<void(bool)> onMuteChanged_;
};
