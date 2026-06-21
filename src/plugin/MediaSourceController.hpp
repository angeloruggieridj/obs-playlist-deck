#pragma once
#include <functional>
#include <string>
#include <vector>
#include <obs.h>

// Wraps the libobs calls needed to drive an existing media source
// (ffmpeg_source / vlc_source) from the playlist. Keeps a strong reference to
// the bound source and subscribes to its "media_ended" signal for auto-advance.
class MediaSourceController {
public:
    ~MediaSourceController();

    static std::vector<std::string> listMediaSources();

    void bind(const std::string& sourceName);
    void unbind();
    bool isBound() const { return source_ != nullptr; }
    std::string boundName() const { return boundName_; }

    bool setFileAndRestart(const std::string& path);
    bool setFileLoadOnly(const std::string& path); // set file, then pause on first frame
    long long currentDurationMs() const;           // -1 if unknown / not bound
    long long currentTimeMs() const;               // playback position, -1 if not bound
    bool isPlaying() const;
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void restart();

    void setOnMediaEnded(std::function<void()> cb) { onEnded_ = std::move(cb); }
    // Fires when the bound source leaves the program output (e.g. studio-mode
    // program -> preview transition).
    void setOnDeactivated(std::function<void()> cb) { onDeactivated_ = std::move(cb); }

private:
    static void mediaEndedThunk(void* data, calldata_t* cd);
    static void deactivateThunk(void* data, calldata_t* cd);

    obs_source_t* source_ = nullptr; // strong ref while bound
    std::string boundName_;
    std::function<void()> onEnded_;
    std::function<void()> onDeactivated_;
};
