// SPDX-License-Identifier: MIT
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-websocket-api.h>
#include <QMainWindow>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include "PlaylistDock.hpp"
#include "VendorBridge.hpp"

#ifndef PLD_VERSION
#define PLD_VERSION "0.0.0"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-playlist-deck", "en-US")

MODULE_EXPORT const char* obs_module_description(void) {
    return "Playlist Deck - drive an OBS media source from a native playlist dock";
}
MODULE_EXPORT const char* obs_module_name(void) { return "Playlist Deck"; }

static PlaylistDock* g_dock = nullptr;
static obs_websocket_vendor g_vendor = nullptr;
static constexpr const char* DOCK_ID = "obs-playlist-deck-dock";

namespace pld {
void emitVendorEvent(const char* name, obs_data_t* data) {
    if (!g_vendor || !name) return;
    obs_websocket_vendor_emit_event(g_vendor, name, data);
}
} // namespace pld

static void on_frontend_event(enum obs_frontend_event event, void*) {
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
        // The dock already exists (registered from obs_module_load); only now is
        // there a scene collection to bind to.
        if (g_dock) g_dock->frontendLoaded();
        break;
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        if (g_dock) g_dock->refreshSources();
        break;
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
        // The scene on air changed, so the bound source may just have left
        // Program — which is what a staged clip is waiting for. The source's own
        // "deactivate" signal does not fire for this: in studio mode a source in
        // the preview scene still counts as active, so the ordinary
        // program -> preview transition raised nothing at all and the staged
        // clip was never loaded.
        if (g_dock) {
            g_dock->refreshSources();
            g_dock->programLayoutChanged();
        }
        break;
    case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
    case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
        if (g_dock) g_dock->programLayoutChanged();
        break;
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
        // The dock keeps a strong obs_source_t reference while bound. It must
        // be released before OBS begins unloading the outgoing collection.
        if (g_dock) g_dock->releaseSource();
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
// Callbacks run on the websocket thread. Anything that touches the playlist is
// marshalled to the dock's (main) thread; anything that reads state reads the
// dock's snapshot, which is published under a mutex.
//
// GetStatus used to call straight into the model from this thread while the UI
// thread was adding to or erasing from the same std::vector. That is a data
// race by construction, and a read landing inside an erase is undefined
// behaviour — the kind of crash that only ever happens during a busy show.

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
static void ws_clear(obs_data_t*, obs_data_t* resp, void*) {
    obs_data_set_bool(resp, "ok", invoke_simple("wsClear"));
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
static void ws_set_mode(obs_data_t* req, obs_data_t* resp, void*) {
    bool ok = false;
    if (g_dock && obs_data_has_user_value(req, "mode")) {
        int mode = static_cast<int>(obs_data_get_int(req, "mode"));
        ok = QMetaObject::invokeMethod(g_dock, "wsSetMode", Qt::QueuedConnection, Q_ARG(int, mode));
    }
    obs_data_set_bool(resp, "ok", ok);
}
static void ws_seek(obs_data_t* req, obs_data_t* resp, void*) {
    bool ok = false;
    if (g_dock && obs_data_has_user_value(req, "positionMs")) {
        int ms = static_cast<int>(obs_data_get_int(req, "positionMs"));
        ok = QMetaObject::invokeMethod(g_dock, "wsSeek", Qt::QueuedConnection, Q_ARG(int, ms));
    }
    obs_data_set_bool(resp, "ok", ok);
}
static void ws_add_paths(obs_data_t* req, obs_data_t* resp, void*) {
    bool ok = false;
    obs_data_array_t* arr = obs_data_get_array(req, "paths");
    if (g_dock && arr) {
        QStringList paths;
        const size_t count = obs_data_array_count(arr);
        for (size_t i = 0; i < count; ++i) {
            obs_data_t* entry = obs_data_array_item(arr, i);
            if (!entry) continue;
            const char* p = obs_data_get_string(entry, "value");
            if (p && *p) paths << QString::fromUtf8(p);
            obs_data_release(entry);
        }
        if (!paths.isEmpty())
            ok = QMetaObject::invokeMethod(g_dock, "wsAddPaths", Qt::QueuedConnection,
                                           Q_ARG(QStringList, paths));
    }
    if (arr) obs_data_array_release(arr);
    obs_data_set_bool(resp, "ok", ok);
}

static void ws_get_status(obs_data_t*, obs_data_t* resp, void*) {
    if (!g_dock) {
        obs_data_set_bool(resp, "ok", false);
        return;
    }
    const DeckStatus s = g_dock->status();
    // v1 fields, unchanged, so existing clients keep working.
    obs_data_set_int(resp, "count", s.count);
    obs_data_set_int(resp, "currentIndex", s.currentIndex);
    // v2 additions.
    obs_data_set_string(resp, "currentTitle", s.currentTitle.toUtf8().constData());
    obs_data_set_string(resp, "currentPath", s.currentPath.toUtf8().constData());
    obs_data_set_int(resp, "positionMs", s.positionMs);
    obs_data_set_int(resp, "durationMs", s.durationMs);
    obs_data_set_bool(resp, "playing", s.playing);
    obs_data_set_bool(resp, "paused", s.paused);
    obs_data_set_bool(resp, "sourceBound", s.sourceBound);
    obs_data_set_string(resp, "sourceName", s.sourceName.toUtf8().constData());
    obs_data_set_int(resp, "mode", s.mode);
    obs_data_set_string(resp, "modeName", s.modeName.toUtf8().constData());
    obs_data_set_string(resp, "playlistName", s.playlistName.toUtf8().constData());
    obs_data_set_int(resp, "upNextIndex", s.upNextIndex);
    obs_data_set_string(resp, "upNextTitle", s.upNextTitle.toUtf8().constData());
    obs_data_set_string(resp, "pluginVersion", PLD_VERSION);
    obs_data_set_bool(resp, "ok", true);
}

static void ws_get_items(obs_data_t* req, obs_data_t* resp, void*) {
    if (!g_dock) {
        obs_data_set_bool(resp, "ok", false);
        return;
    }
    const DeckStatus s = g_dock->status();
    // Paginated: a set list can be long, and a websocket reply is not the place
    // to discover that.
    const int total = static_cast<int>(s.items.size());
    int from = static_cast<int>(obs_data_get_int(req, "from"));
    int to = obs_data_has_user_value(req, "to") ? static_cast<int>(obs_data_get_int(req, "to"))
                                                : total - 1;
    if (from < 0) from = 0;
    if (to >= total) to = total - 1;
    obs_data_array_t* arr = obs_data_array_create();
    for (int i = from; i <= to; ++i) {
        obs_data_t* entry = obs_data_create();
        obs_data_set_int(entry, "index", i);
        obs_data_set_string(entry, "title", s.items[i].first.toUtf8().constData());
        obs_data_set_string(entry, "path", s.items[i].second.toUtf8().constData());
        obs_data_array_push_back(arr, entry);
        obs_data_release(entry);
    }
    obs_data_set_array(resp, "items", arr);
    obs_data_array_release(arr);
    obs_data_set_int(resp, "count", s.count);
    obs_data_set_bool(resp, "ok", true);
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
    // v2
    obs_websocket_vendor_register_request(g_vendor, "GetItems", ws_get_items, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "SetMode", ws_set_mode, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "Seek", ws_seek, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "AddPaths", ws_add_paths, nullptr);
    obs_websocket_vendor_register_request(g_vendor, "Clear", ws_clear, nullptr);
    blog(LOG_INFO, "[obs-playlist-deck] obs-websocket vendor registered");
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[obs-playlist-deck] loaded");
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    // Register the dock here, not on FINISHED_LOADING: OBS restores the saved
    // dock layout right after all modules are loaded and before that event, so a
    // dock added later is never given back its visibility, position or size.
    // obs_frontend_add_dock_by_id() is also what adds the checkable entry to the
    // Docks menu; the custom-qdock API deliberately adds neither, leaving the
    // dock unmanaged and with no way to toggle it from the UI.
    auto* mw = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    g_dock = new PlaylistDock(mw);
    if (!obs_frontend_add_dock_by_id(DOCK_ID, PlaylistDock::dockTitle().toUtf8().constData(), g_dock)) {
        blog(LOG_ERROR, "[obs-playlist-deck] dock registration failed");
        delete g_dock;
        g_dock = nullptr;
        // obs_module_unload() is not called when load fails, so undo the callback here.
        obs_frontend_remove_event_callback(on_frontend_event, nullptr);
        return false;
    }
    blog(LOG_INFO, "[obs-playlist-deck] dock registered");
    return true;
}

MODULE_EXPORT void obs_module_post_load(void) {
    // obs-websocket is guaranteed loaded by now; register the vendor API.
    register_vendor();
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    // No further vendor events after this point: the dock is going away and the
    // vendor handle belongs to obs-websocket.
    g_vendor = nullptr;
    g_dock = nullptr;
}
