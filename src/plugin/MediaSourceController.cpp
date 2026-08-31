// SPDX-License-Identifier: MIT
#include "MediaSourceController.hpp"

#include <obs-frontend-api.h>

using pld::SourceKind;

std::vector<std::string> MediaSourceController::listMediaSources() {
    std::vector<std::string> out;
    auto cb = [](void* param, obs_source_t* src) -> bool {
        if (pld::isBindableSourceId(obs_source_get_id(src))) {
            const char* name = obs_source_get_name(src);
            if (name) static_cast<std::vector<std::string>*>(param)->emplace_back(name);
        }
        return true;
    };
    obs_enum_sources(cb, &out);
    return out;
}

MediaSourceController::~MediaSourceController() { unbind(); }

void MediaSourceController::bind(const std::string& sourceName) {
    unbind();
    obs_source_t* s = obs_get_source_by_name(sourceName.c_str());
    if (!s) return;
    source_ = s; // keep strong ref
    boundName_ = sourceName;
    // The kind decides which setting every later write targets. Recording it
    // once at bind time keeps the decision in one place.
    kind_ = pld::sourceKindFromId(obs_source_get_id(source_));
    signal_handler_t* sh = obs_source_get_signal_handler(source_);
    if (sh) {
        signal_handler_connect(sh, "media_ended", &MediaSourceController::mediaEndedThunk, this);
        signal_handler_connect(sh, "media_started", &MediaSourceController::mediaStartedThunk, this);
        signal_handler_connect(sh, "deactivate", &MediaSourceController::deactivateThunk, this);
        signal_handler_connect(sh, "mute", &MediaSourceController::muteThunk, this);
    }
}

void MediaSourceController::unbind() {
    if (source_) {
        signal_handler_t* sh = obs_source_get_signal_handler(source_);
        if (sh) {
            signal_handler_disconnect(sh, "media_ended", &MediaSourceController::mediaEndedThunk, this);
            signal_handler_disconnect(sh, "media_started", &MediaSourceController::mediaStartedThunk,
                                      this);
            signal_handler_disconnect(sh, "deactivate", &MediaSourceController::deactivateThunk, this);
            signal_handler_disconnect(sh, "mute", &MediaSourceController::muteThunk, this);
        }
        obs_source_release(source_);
        source_ = nullptr;
    }
    boundName_.clear();
    kind_ = SourceKind::Unsupported;
}

// The heart of the VLC fix. OBS's two media sources do not share a single
// setting for "the file to play":
//
//   ffmpeg_source  is_local_file = true, local_file = "<path>"
//   vlc_source     playlist = [ { "value": "<path>" } ]   (an obs_data_array)
//
// The controller used to write local_file on both, which vlc_source simply
// ignores: picking a playlist item never changed what a bound VLC source was
// playing, and currentFile() always read back "", so the stale-file cleanup on
// startup had nothing to compare against either.
bool MediaSourceController::writeFile(const std::string& path) {
    if (!source_) return false;
    obs_data_t* settings = obs_source_get_settings(source_);
    if (!settings) return false;
    switch (kind_) {
    case SourceKind::Ffmpeg:
        obs_data_set_bool(settings, "is_local_file", true);
        obs_data_set_string(settings, "local_file", path.c_str());
        break;
    case SourceKind::Vlc: {
        obs_data_array_t* arr = obs_data_array_create();
        if (!path.empty()) {
            obs_data_t* entry = obs_data_create();
            obs_data_set_string(entry, "value", path.c_str());
            obs_data_array_push_back(arr, entry);
            obs_data_release(entry);
        }
        obs_data_set_array(settings, "playlist", arr);
        obs_data_array_release(arr);
        // vlc_source loops its playlist by default, and a looping playlist never
        // reaches its end — so "media_ended" would never fire and auto-advance
        // would be dead on arrival. The deck owns the sequencing; VLC plays one
        // clip and reports that it finished.
        obs_data_set_bool(settings, "loop", false);
        obs_data_set_bool(settings, "shuffle", false);
        break;
    }
    case SourceKind::Unsupported:
        obs_data_release(settings);
        return false;
    }
    obs_source_update(source_, settings);
    obs_data_release(settings);
    return true;
}

bool MediaSourceController::setFileAndRestart(const std::string& path) {
    if (!writeFile(path)) return false;
    obs_source_media_restart(source_);
    return true;
}

bool MediaSourceController::setFileLoadOnly(const std::string& path) {
    if (!writeFile(path)) return false;
    // Restart to load the first frame, then immediately pause so the clip is
    // staged but not playing (even while the scene is live in program). The
    // pause is repeated when the source reports it started, because the decoder
    // may not have opened the file yet at this point.
    obs_source_media_restart(source_);
    obs_source_media_play_pause(source_, true);
    return true;
}

bool MediaSourceController::clearFile() {
    if (!writeFile("")) return false;
    obs_source_media_stop(source_);
    return true;
}

std::string MediaSourceController::currentFile() const {
    if (!source_) return {};
    obs_data_t* settings = obs_source_get_settings(source_);
    if (!settings) return {};
    std::string out;
    if (kind_ == SourceKind::Vlc) {
        obs_data_array_t* arr = obs_data_get_array(settings, "playlist");
        if (arr) {
            if (obs_data_array_count(arr) > 0) {
                obs_data_t* first = obs_data_array_item(arr, 0);
                if (first) {
                    const char* v = obs_data_get_string(first, "value");
                    if (v) out = v;
                    obs_data_release(first);
                }
            }
            obs_data_array_release(arr);
        }
    } else if (kind_ == SourceKind::Ffmpeg) {
        const char* f = obs_data_get_string(settings, "local_file");
        if (f) out = f;
    }
    obs_data_release(settings);
    return out;
}

long long MediaSourceController::currentDurationMs() const {
    if (!source_) return -1;
    int64_t d = obs_source_media_get_duration(source_);
    return d > 0 ? static_cast<long long>(d) : -1;
}

long long MediaSourceController::currentTimeMs() const {
    if (!source_) return -1;
    int64_t t = obs_source_media_get_time(source_);
    return t >= 0 ? static_cast<long long>(t) : -1;
}

bool MediaSourceController::isPlaying() const {
    return source_ && obs_source_media_get_state(source_) == OBS_MEDIA_STATE_PLAYING;
}

bool MediaSourceController::isPaused() const {
    return source_ && obs_source_media_get_state(source_) == OBS_MEDIA_STATE_PAUSED;
}

void MediaSourceController::play() {
    if (source_) obs_source_media_play_pause(source_, false);
}
void MediaSourceController::pause() {
    if (source_) obs_source_media_play_pause(source_, true);
}
void MediaSourceController::togglePlayPause() {
    if (!source_) return;
    if (obs_source_media_get_state(source_) == OBS_MEDIA_STATE_PLAYING)
        obs_source_media_play_pause(source_, true);
    else
        obs_source_media_play_pause(source_, false);
}
void MediaSourceController::stop() {
    if (source_) obs_source_media_stop(source_);
}
void MediaSourceController::restart() {
    if (source_) obs_source_media_restart(source_);
}

bool MediaSourceController::isMuted() const {
    return source_ && obs_source_muted(source_);
}

void MediaSourceController::setMuted(bool muted) {
    // obs_source_set_muted raises the source's "mute" signal, so the dock hears
    // about its own change through the same path as a change from the mixer:
    // one way in, one way out, no state to keep in sync.
    if (source_) obs_source_set_muted(source_, muted);
}

void MediaSourceController::toggleMute() { setMuted(!isMuted()); }

bool MediaSourceController::seekMs(long long ms) {
    if (!source_ || ms < 0) return false;
    obs_source_media_set_time(source_, static_cast<int64_t>(ms));
    return true;
}

bool MediaSourceController::isInProgram() const {
    if (!source_) return false;
    // obs_frontend_get_current_scene() is the scene on air, in studio mode as
    // well as out of it, which is exactly the question being asked.
    obs_source_t* program = obs_frontend_get_current_scene();
    if (!program) return false;
    bool found = (program == source_);
    if (!found) {
        obs_scene_t* scene = obs_scene_from_source(program);
        // Recursive: the source may sit inside a group rather than at the top
        // level of the scene.
        if (scene) found = obs_scene_find_source_recursive(scene, boundName_.c_str()) != nullptr;
    }
    obs_source_release(program);
    return found;
}

void MediaSourceController::mediaEndedThunk(void* data, calldata_t*) {
    auto* self = static_cast<MediaSourceController*>(data);
    if (self && self->onEnded_) self->onEnded_();
}

void MediaSourceController::mediaStartedThunk(void* data, calldata_t*) {
    auto* self = static_cast<MediaSourceController*>(data);
    if (self && self->onStarted_) self->onStarted_();
}

void MediaSourceController::muteThunk(void* data, calldata_t* cd) {
    auto* self = static_cast<MediaSourceController*>(data);
    if (self && self->onMuteChanged_) self->onMuteChanged_(calldata_bool(cd, "muted"));
}

void MediaSourceController::deactivateThunk(void* data, calldata_t*) {
    auto* self = static_cast<MediaSourceController*>(data);
    if (self && self->onDeactivated_) self->onDeactivated_();
}
