#include "MediaSourceController.hpp"
#include <cstring>

static bool isMediaId(const char* id) {
    return id && (std::strcmp(id, "ffmpeg_source") == 0 || std::strcmp(id, "vlc_source") == 0);
}

std::vector<std::string> MediaSourceController::listMediaSources() {
    std::vector<std::string> out;
    auto cb = [](void* param, obs_source_t* src) -> bool {
        const char* id = obs_source_get_id(src);
        if (isMediaId(id)) {
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
    signal_handler_t* sh = obs_source_get_signal_handler(source_);
    if (sh) signal_handler_connect(sh, "media_ended", &MediaSourceController::mediaEndedThunk, this);
}

void MediaSourceController::unbind() {
    if (source_) {
        signal_handler_t* sh = obs_source_get_signal_handler(source_);
        if (sh)
            signal_handler_disconnect(sh, "media_ended", &MediaSourceController::mediaEndedThunk, this);
        obs_source_release(source_);
        source_ = nullptr;
    }
    boundName_.clear();
}

bool MediaSourceController::setFileAndRestart(const std::string& path) {
    if (!source_) return false;
    obs_data_t* settings = obs_source_get_settings(source_);
    obs_data_set_bool(settings, "is_local_file", true);
    obs_data_set_string(settings, "local_file", path.c_str());
    obs_source_update(source_, settings);
    obs_data_release(settings);
    obs_source_media_restart(source_);
    return true;
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

void MediaSourceController::mediaEndedThunk(void* data, calldata_t*) {
    auto* self = static_cast<MediaSourceController*>(data);
    if (self && self->onEnded_) self->onEnded_();
}
