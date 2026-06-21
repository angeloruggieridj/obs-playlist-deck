#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-websocket-api.h>
#include <QMainWindow>
#include <QMetaObject>
#include <QString>
#include "PlaylistDock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-playlist-deck", "en-US")

MODULE_EXPORT const char* obs_module_description(void) {
    return "Playlist Deck - drive an OBS media source from a native playlist dock";
}
MODULE_EXPORT const char* obs_module_name(void) { return "Playlist Deck"; }

static PlaylistDock* g_dock = nullptr;
static obs_websocket_vendor g_vendor = nullptr;
static constexpr const char* DOCK_ID = "obs-playlist-deck-dock";

static void on_frontend_event(enum obs_frontend_event event, void*) {
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
        auto* mw = static_cast<QMainWindow*>(obs_frontend_get_main_window());
        g_dock = new PlaylistDock(mw);
        obs_frontend_add_custom_qdock(DOCK_ID, g_dock);
        blog(LOG_INFO, "[obs-playlist-deck] dock registered");
        break;
    }
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        if (g_dock) g_dock->refreshSources();
        break;
    case OBS_FRONTEND_EVENT_EXIT:
        // Release libobs resources while libobs is still alive, before Qt tears
        // the dock down during shutdown.
        if (g_dock) g_dock->shutdown();
        break;
    default:
        break;
    }
}

// ---- obs-websocket vendor requests ---------------------------------------
// Callbacks run on the websocket thread; marshal to the dock's (main) thread.

static bool invoke_simple(const char* method) {
    if (!g_dock) return false;
    return QMetaObject::invokeMethod(g_dock, method, Qt::QueuedConnection);
}

static void ws_next(obs_data_t*, obs_data_t* resp, void*) {
    obs_data_set_bool(resp, "ok", invoke_simple("wsNext"));
}
static void ws_prev(obs_data_t*, obs_data_t* resp, void*) {
    obs_data_set_bool(resp, "ok", invoke_simple("wsPrev"));
}
static void ws_stop(obs_data_t*, obs_data_t* resp, void*) {
    obs_data_set_bool(resp, "ok", invoke_simple("wsStop"));
}
static void ws_playpause(obs_data_t*, obs_data_t* resp, void*) {
    obs_data_set_bool(resp, "ok", invoke_simple("wsPlayPause"));
}
static void ws_play_index(obs_data_t* req, obs_data_t* resp, void*) {
    bool ok = false;
    if (g_dock && obs_data_has_user_value(req, "index")) {
        int idx = static_cast<int>(obs_data_get_int(req, "index"));
        ok = QMetaObject::invokeMethod(g_dock, "wsPlayIndex", Qt::QueuedConnection, Q_ARG(int, idx));
    }
    obs_data_set_bool(resp, "ok", ok);
}
static void ws_load(obs_data_t* req, obs_data_t* resp, void*) {
    bool ok = false;
    const char* path = obs_data_get_string(req, "path");
    if (g_dock && path && *path) {
        ok = QMetaObject::invokeMethod(g_dock, "wsLoad", Qt::QueuedConnection,
                                       Q_ARG(QString, QString::fromUtf8(path)));
    }
    obs_data_set_bool(resp, "ok", ok);
}
static void ws_get_status(obs_data_t*, obs_data_t* resp, void*) {
    if (g_dock) {
        obs_data_set_int(resp, "count", g_dock->itemCount());
        obs_data_set_int(resp, "currentIndex", g_dock->currentIndex());
        obs_data_set_bool(resp, "ok", true);
    } else {
        obs_data_set_bool(resp, "ok", false);
    }
}

static void register_vendor() {
    g_vendor = obs_websocket_register_vendor("obs-playlist-deck");
    if (!g_vendor) return; // obs-websocket not installed; remote control unavailable
    obs_websocket_vendor_register_request(g_vendor, "Next", ws_next, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "Previous", ws_prev, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "Stop", ws_stop, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "PlayPause", ws_playpause, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "PlayIndex", ws_play_index, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "Load", ws_load, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "GetStatus", ws_get_status, nullptr);
    blog(LOG_INFO, "[obs-playlist-deck] obs-websocket vendor registered");
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[obs-playlist-deck] loaded");
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    return true;
}

MODULE_EXPORT void obs_module_post_load(void) {
    // obs-websocket is guaranteed loaded by now; register the vendor API.
    register_vendor();
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    g_dock = nullptr;
}
