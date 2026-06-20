# Playlist Deck for OBS

Native OBS Studio dock that manages a playlist of local media files and drives
an **existing OBS media source** from it. Selecting a playlist item sets that
source's file, restarts playback, and (optionally) auto-advances to the next
item when the current one ends — so you never edit the source path by hand while
live.

No browser source, no embedded web server, no HTML player. Pure OBS + Qt.

## Features

- Native Qt dock living inside OBS.
- Bind to any existing **Media Source** (`ffmpeg_source`) or **VLC Source**
  via a dropdown.
- Playlist with add / remove / reorder / clear and transport controls
  (play, pause, stop, prev, next).
- Auto-advance on media end, with optional looping (wrap).
- Named playlists: save / load / delete (stored in the OBS module config).
- Import / Export playlists as `.json` or `.m3u/.m3u8`.
- Global OBS hotkeys: next, previous, play/pause, stop.
- Cross-platform: Windows x64, Linux x86_64, macOS universal (Intel + Apple
  Silicon).

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

1. Add a **Media Source** (or VLC Source) to your scene in OBS.
2. In the Playlist Deck dock, pick that source from the **Media source**
   dropdown.
3. Add media files, then double-click an item (or "Play selected") to play it
   through that source. Enable **Loop playlist** for continuous wrap-around.

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
for the exact CI build steps per platform.

## License

MIT — see [LICENSE](LICENSE).
