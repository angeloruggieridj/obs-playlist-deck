# Playlist Deck for OBS

Native OBS Studio dock that manages a playlist of local media files and drives
an **existing OBS media source** from it. Selecting a playlist item sets that
source's file, restarts playback, and (optionally) auto-advances to the next
item when the current one ends — so you never edit the source path by hand while
live.

No browser source, no embedded web server, no HTML player. Pure OBS + Qt.

## Features

- Native Qt dock living inside OBS — no browser source, no web server.
- Bind to any existing **Media Source** (`ffmpeg_source`) or **VLC Source**
  via a dropdown.
- Playlist with add / remove / reorder / clear and transport controls
  (play, prev, play-pause, stop, next), with each item's **duration** shown.
- **On end** behavior selector:
  - *Play next* — auto-advance to the next item.
  - *Loop* — auto-advance and wrap around.
  - *Load next (paused)* — hold the finished clip's last frame on program and
    stage the next clip (paused, off-air) only when the source leaves program
    (studio-mode program → preview). The next clip's first frame never goes
    live and the playlist never auto-advances on air.
  - *Stop* — stop at the end of the clip.
- Extra modes: **Shuffle** (random next) and **Repeat one**.
- **Drag & drop** files from the OS file manager into the list; reorder by drag.
  Missing files are highlighted; a **filter box** narrows long playlists.
- Now-playing **progress bar** with elapsed / total / remaining time.
- Save / open playlists as **`.json` or `.m3u/.m3u8`** to a location you choose;
  the loaded playlist file is shown in the dock.
- Optional **auto-restore** of the last playlist on startup; **background**
  duration probing so large playlists load instantly (Settings dialog).
- Global OBS hotkeys: next, previous, play/pause, stop.
- **Remote control** via obs-websocket (vendor `obs-playlist-deck`) and an
  included **Stream Deck companion** — see below.
- **Localized** UI: English, Italian, Spanish, French, German, Portuguese (BR),
  Russian, Chinese (Simplified), Japanese, Korean — selectable, or follow OBS's
  language (other OBS languages fall back to English).
- Remembers the **On end** mode and the bound source across restarts.
- Shows the plugin version and links to the latest release when an update is
  available (manual download — no auto-install).
- Cross-platform: Windows x64, Linux x86_64, macOS universal (Intel + Apple
  Silicon). Requires **OBS Studio 30.0+** (built and tested against 32.1.2).

## Remote control (obs-websocket) & Stream Deck

Playlist Deck registers an obs-websocket **vendor** named `obs-playlist-deck`
with requests: `Next`, `Previous`, `PlayPause`, `Stop`, `PlayIndex` (`{index}`),
`Load` (`{path}`), `GetStatus`. Call them via obs-websocket v5
`CallVendorRequest` from any client or script.

An Elgato **Stream Deck companion** lives in [`streamdeck/`](streamdeck/) with
Next / Previous / Play-Pause / Stop / Play Item actions. It is buildless (plain
JS/HTML). Download `obs-playlist-deck-streamdeck.zip` from a release (icons
pre-rendered) and copy the `.sdPlugin` folder into the Stream Deck plugins
directory, or build it yourself — see [`streamdeck/README.md`](streamdeck/README.md).

## Install

Download the latest build for your platform from the
[Releases](https://github.com/angeloruggieridj/obs-playlist-deck/releases) page.

### Windows
Extract the zip into your OBS install folder (so `obs-plugins/64bit/` and
`data/obs-plugins/` merge with OBS's).

### Linux
```bash
sudo tar -xzf obs-playlist-deck-linux-x86_64.tar.gz -C /
```
(For a system OBS install — not Flatpak/Snap.)

### macOS (universal — Intel & Apple Silicon)
```bash
PLUGIN_DIR="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$PLUGIN_DIR"
tar -xzf obs-playlist-deck-macos-universal.tar.gz -C "$PLUGIN_DIR"
# remove the download quarantine so OBS can load the unsigned plugin:
xattr -dr com.apple.quarantine "$PLUGIN_DIR/obs-playlist-deck.plugin"
```
Then open OBS → the **Playlist Deck** dock appears under the *Docks* menu.

## Usage

1. Add a **Media Source** (or VLC Source) to a scene in OBS.
2. In the Playlist Deck dock, pick that source from the **Media source**
   dropdown.
3. **Add** media files, then double-click an item (or select it and press
   **Play**) to play it through that source.
4. Pick an **On end** behavior. Use **Save** / **Open** to keep playlists as
   files you choose (`.json` or `.m3u`).

### macOS Gatekeeper note

The macOS build is ad-hoc signed (not notarized), so after downloading you must
clear the download quarantine once, otherwise OBS won't load it:

```bash
xattr -dr com.apple.quarantine "$HOME/Library/Application Support/obs-studio/plugins/obs-playlist-deck.plugin"
```

## Build from source

Requires CMake ≥ 3.22, a C++17 compiler, Qt 6, and OBS development files
(`libobs`, `obs-frontend-api`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the unit tests (no OBS/Qt needed):
```bash
cmake -B build-tests -DBUILD_PLUGIN=OFF -DBUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

See [.github/workflows/build_project.yml](.github/workflows/build_project.yml)
for the exact CI build steps per platform. CI builds OBS dev libraries from
source (cached per OBS version), builds the plugin, and — for the Stream Deck
companion — renders the SVG icons to PNG with `rsvg-convert` and zips the
`.sdPlugin` into `obs-playlist-deck-streamdeck.zip`. A `compat` matrix
additionally compiles the plugin against OBS 30/31 SDKs to inform the supported
minimum version.

### Localization

UI strings live in [`data/locale/*.ini`](data/locale/); `en-US.ini` is the
canonical key set. To add a language, copy it to `<code>.ini`, translate the
values, and (optionally) add it to the language selector in
`src/plugin/PlaylistDock.cpp`. Missing keys fall back to English automatically.

## License

MIT — see [LICENSE](LICENSE).
