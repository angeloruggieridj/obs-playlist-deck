# obs-playlist-deck — Design

**Date:** 2026-06-20
**Status:** Approved

## Summary

A native OBS Studio plugin that adds a dock for managing playlists of local
media files. The selected playlist item drives the `local_file` of an
**existing OBS media source** chosen by the user, so the streamer never edits
the source path by hand while live. No browser source, no embedded HTTP server,
no HTML player — everything is native OBS + Qt.

This is a clean reimagining of `Substazz/studio-player`, keeping its useful
ideas (named playlists, import/export, hotkeys, transport controls, reorder)
but replacing the browser-based playback with native media-source control.

Cross-platform: Windows x64, Linux x86_64, macOS universal2 (arm64 + x86_64).

## Goals

- Manage an ordered playlist of local media files in a native OBS dock.
- Bind the playlist to a user-selected existing media source; selecting an item
  sets that source's file, restarts playback, and auto-advances on end.
- Named playlists (save/load/delete), file import/export (JSON + M3U), global
  OBS hotkeys, drag/reorder and transport controls.
- Rigorous unit tests for all non-OBS logic.
- CI builds for all three platforms; macOS artifact is a universal `.plugin`.

## Non-Goals (YAGNI)

- No browser source, HTTP server, or HTML player.
- No web/URL sources, no network metadata fetching.
- No volume slider — OBS's native audio mixer already controls media volume.

## Architecture

Three layers. The **core** has zero OBS/Qt dependencies so it can be unit-tested
without an OBS runtime and compiles in milliseconds on every platform.

```
src/core/        Pure C++17, no OBS/Qt includes  → linked by the unit-test target
  Playlist       ordered items, current index, add/remove/move/clear,
                 next()/prev() with optional wrap, auto-advance step
  PlaylistIO     JSON serialize/deserialize (full fidelity) +
                 M3U/M3U8 parse/write
  MediaPath      media file extension filter, path normalization helpers
src/obs/         OBS glue: enumerate media sources, set local_file + restart,
                 media_ended signal subscription, hotkey registration
src/ui/          PlaylistDockWidget (Qt6 Widgets): list, source dropdown,
                 transport buttons, named-playlist controls, import/export
plugin-main.cpp  OBS_DECLARE_MODULE; registers the dock on
                 OBS_FRONTEND_EVENT_FINISHED_LOADING
```

### Unit boundaries

- **Playlist**: owns the item vector and current index. Knows nothing about
  files on disk or OBS. Pure value logic — fully deterministic, fully tested.
- **PlaylistIO**: free functions `toJson`/`fromJson`/`toM3u`/`parseM3u` over a
  `Playlist`/`std::vector<PlaylistItem>`. No I/O side effects beyond string
  in/out (the caller does the actual file read/write), so tests pass strings.
- **MediaPath**: free functions; pure string predicates.
- **PlaylistDockWidget**: thin Qt layer translating UI events into Playlist
  mutations and OBS calls. Holds a `Playlist` instance.
- **OBS glue**: free functions wrapping libobs calls, isolated so the UI does
  not sprinkle libobs calls everywhere.

## Data Model

```cpp
struct PlaylistItem {
    std::string path;    // absolute local file path
    std::string title;   // display name; defaults to filename stem
};

class Playlist {
    std::vector<PlaylistItem> items;
    int current = -1;     // -1 == nothing selected
    // add, removeAt, moveUp/moveDown (or move(from,to)), clear,
    // setCurrent(i), next(bool wrap), prev(bool wrap)
};
```

Persisted JSON shape (named playlists and `.json` export):

```json
{
  "version": 1,
  "name": "Intro clips",
  "items": [
    { "path": "/abs/clip1.mp4", "title": "Clip 1" },
    { "path": "/abs/clip2.mov", "title": "Clip 2" }
  ]
}
```

M3U: standard `#EXTM3U` / `#EXTINF:<secs>,<title>` lines + path lines; comments
and blank lines ignored; `#EXTINF` title (if present) becomes the item title,
else the filename stem. CRLF and LF both accepted.

## Media Source Binding

- Source dropdown is populated via `obs_enum_sources`, filtered to source ids
  `ffmpeg_source` and `vlc_source`, listing source names. Refreshed on frontend
  events (scene/source list changes) and on a manual refresh button.
- On item selection (or next/prev/auto-advance):
  1. `obs_get_source_by_name(name)`.
  2. `obs_source_get_settings`, set `local_file` = item path, `is_local_file` =
     true, `obs_source_update`, then `obs_source_media_restart`.
  3. Release refs.
- **Auto-advance:** subscribe to the selected source's `media_ended` signal via
  its `signal_handler`. On signal, advance to next (wrap configurable) on the Qt
  thread (queued), set + restart. Unsubscribe cleanly when the bound source
  changes, is destroyed, or on module unload.
- **Transport:** play/pause → `obs_source_media_play_pause`; stop →
  `obs_source_media_stop`; restart current → `obs_source_media_restart`.

## Persistence & I/O

- Named playlists saved as JSON files under `obs_module_config_path("playlists")`;
  load/delete/list drive a dropdown.
- Import/Export via Qt file dialog to `.json` or `.m3u/.m3u8`. Format chosen by
  file extension. JSON is full fidelity; M3U is interoperable.

## Hotkeys

`obs_hotkey_register_frontend` for: next, prev, play/pause, stop. Saved and
restored through OBS config (`obs_hotkey_save` / load via module data).

## Error Handling

- Missing/renamed bound source: transport/selection is a no-op with a status-bar
  message; the dropdown refresh drops stale entries.
- Missing file on disk: item still selectable; status bar warns; OBS media source
  shows its own error. Playlist does not crash.
- Malformed import file: parser returns an error/empty result; UI shows a status
  message and leaves the current playlist unchanged.
- All libobs handles released; signal handlers disconnected on teardown.

## Testing

- **Framework:** doctest (single header, vendored under `tests/vendor`).
- **Target:** `playlist-deck-tests` links only `src/core/` — no OBS/Qt.
- **Cases:**
  - Playlist: add/remove/move/clear; current-index invariants after each
    mutation; next/prev with and without wrap; auto-advance from last item;
    empty-playlist edge cases.
  - PlaylistIO JSON: round-trip equality; unknown fields ignored; missing
    optional title defaults to filename stem; version handling.
  - PlaylistIO M3U: parse `#EXTM3U`/`#EXTINF`/comments/blank lines; paths with
    spaces; CRLF vs LF; write→parse round-trip; title fallback.
  - MediaPath: extension filter accepts common media, rejects others;
    normalization.
- **Run:** CTest in a dedicated CI `tests` job (Linux, fast). Packaging jobs
  depend on tests passing.

## Build & CI

Based on the now-green `studio-player` workflow, extended:

- **tests** (ubuntu): configure core + run CTest. Gate for the rest.
- **Linux x86_64**: libobs-dev + qt6 from apt; package tar.gz.
- **Windows x64**: aqtinstall Qt6 with `py7zr<1.0`; build OBS dev
  (`--component Development`); SIMDe + Qt + obs-dev on `CMAKE_PREFIX_PATH`;
  package zip with `obs-plugins/64bit/*.dll` + data.
- **macOS universal2**: universal obs-deps + universal Qt6; build OBS dev with
  `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and the `libobs-metal` subdir
  removed (Swift target, not needed for dev libs); build plugin universal;
  rewrite all absolute Qt install names to `@rpath` (read from `otool -L`);
  assemble `.plugin` bundle (Info.plist, PkgInfo, Resources), ad-hoc codesign;
  verify `lipo -archs` reports `x86_64 arm64` and no non-`@rpath` Qt refs.
- **release** (on tag): publish the three artifacts.

Repository: public `angeloruggieridj/obs-playlist-deck`.

## Open Risks

- Universal OBS-from-source build roughly doubles macOS CI time; acceptable.
- `vlc_source` may be absent if the VLC module isn't installed; dropdown simply
  won't list it — fine.
