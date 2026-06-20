#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include "PlaylistDock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-playlist-deck", "en-US")

MODULE_EXPORT const char* obs_module_description(void) {
    return "Playlist Deck - drive an OBS media source from a native playlist dock";
}
MODULE_EXPORT const char* obs_module_name(void) { return "Playlist Deck"; }

static PlaylistDock* g_dock = nullptr;
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

bool obs_module_load(void) {
    blog(LOG_INFO, "[obs-playlist-deck] loaded");
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    g_dock = nullptr;
}
