<div align="center">

# 🎬 Playlist Deck for OBS

**Queue, play, and control local media through an existing OBS source — from a native dock.**

[![Build](https://github.com/angeloruggieridj/obs-playlist-deck/actions/workflows/build_project.yml/badge.svg)](https://github.com/angeloruggieridj/obs-playlist-deck/actions/workflows/build_project.yml)
[![Latest release](https://img.shields.io/github/v/release/angeloruggieridj/obs-playlist-deck?include_prereleases&sort=semver)](https://github.com/angeloruggieridj/obs-playlist-deck/releases)
[![Downloads](https://img.shields.io/github/downloads/angeloruggieridj/obs-playlist-deck/total)](https://github.com/angeloruggieridj/obs-playlist-deck/releases)
[![License: MIT](https://img.shields.io/github/license/angeloruggieridj/obs-playlist-deck)](LICENSE)

![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20universal-blue)
![OBS](https://img.shields.io/badge/OBS%20Studio-31%2B-302e31?logo=obsstudio)
![Languages](https://img.shields.io/badge/i18n-10%20languages-brightgreen)

</div>

Playlist Deck adds a dock to OBS that manages a playlist of local media files and
drives an **existing OBS media source** from it. Pick a source, build your
playlist, and play items through it — you never edit the source's file path by
hand while live. No browser source, no embedded web server: pure OBS + Qt.

## Table of contents

- [Features](#features)
- [Installation](#installation)
  - [Windows](#windows)
  - [Linux](#linux)
  - [macOS](#macos-universal)
- [Usage](#usage)
- [End-of-clip modes](#end-of-clip-modes)
- [Remote control & Stream Deck](#remote-control--stream-deck)
- [Localization](#localization)
- [Compatibility](#compatibility)
- [Building from source](#building-from-source)
- [License](#license)

## Features

- 🎛️ Native Qt dock inside OBS; bind to any **Media Source** (`ffmpeg_source`)
  or **VLC Source** via a dropdown.
- 📃 Playlist with add / remove / reorder / clear and transport controls; each
  item shows its **duration**.
- 🖱️ **Drag & drop** files from the OS file manager; reorder by drag; missing
  files highlighted; a **filter** box for long playlists.
- 🔀 Playback modes: **Play next**, **Loop**, **Load next (paused)**, **Stop**,
  **Shuffle**, **Repeat one**.
- ⏱️ Now-playing **progress bar** with elapsed / total / remaining time.
- 💾 Save / open playlists as **`.json`** or **`.m3u/.m3u8`** to a location you
  choose; optional **auto-restore** of the last playlist; **background**
  duration probing.
- ⌨️ Global OBS **hotkeys**: next, previous, play/pause, stop.
- 🕹️ **Remote control** via obs-websocket + an included **Stream Deck** companion.
- 🌍 **Localized** UI (10 languages) — selectable or follow OBS.
- 🔔 Built-in update check (links to the latest release; manual download).

## Installation

Download your platform's build from the
[**Releases**](https://github.com/angeloruggieridj/obs-playlist-deck/releases) page.

### Windows
Extract the zip into your per-user OBS plugins folder so you get
`%APPDATA%\obs-studio\plugins\obs-playlist-deck\bin\64bit\obs-playlist-deck.dll`
(and `…\obs-playlist-deck\data\`). No admin rights needed, and it survives OBS
updates.

```powershell
Expand-Archive obs-playlist-deck-windows.zip -DestinationPath "$env:APPDATA\obs-studio\plugins"
```
Then restart OBS.

### Linux
```bash
sudo tar -xzf obs-playlist-deck-linux-x86_64.tar.gz -C /
```
For a system OBS install (not Flatpak/Snap).

### macOS (universal)
```bash
PLUGIN_DIR="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$PLUGIN_DIR"
tar -xzf obs-playlist-deck-macos-universal.tar.gz -C "$PLUGIN_DIR"
# the build is ad-hoc signed (not notarized) — clear the download quarantine once:
xattr -dr com.apple.quarantine "$PLUGIN_DIR/obs-playlist-deck.plugin"
```
Then open OBS → the **Playlist Deck** dock appears under the *Docks* menu.

## Usage

1. Add a **Media Source** (or VLC Source) to a scene in OBS.
2. In the Playlist Deck dock, select it from the **Media source** dropdown.
3. **Add** media files (or drag them in), then double-click an item — or select
   it and press **Play** — to play it through that source.
4. Choose an **End-of-clip** behavior, and use **Save** / **Open** to keep
   playlists as files.

## End-of-clip modes

| Mode | Behavior |
|------|----------|
| **Play next** | Auto-advance to the next item. |
| **Loop** | Auto-advance and wrap around. |
| **Load next (paused)** | Hold the finished clip's last frame on Program; stage the next clip (paused, off-air) only when the source moves Program → Preview in Studio Mode. The next clip never goes live early and the playlist never auto-advances on air. |
| **Stop** | Stop at the end of the clip. |
| **Shuffle** | Play a random next item. |
| **Repeat one** | Replay the current item. |

## Remote control & Stream Deck

Playlist Deck registers an obs-websocket **vendor** named `obs-playlist-deck`
with requests `Next`, `Previous`, `PlayPause`, `Stop`, `PlayIndex` (`{index}`),
`Load` (`{path}`), `GetStatus`. Call them via obs-websocket v5
`CallVendorRequest` from any client or script.

An Elgato **Stream Deck companion** lives in [`streamdeck/`](streamdeck/) with
Next / Previous / Play-Pause / Stop / Play Item actions (buildless JS). Grab
`obs-playlist-deck-streamdeck.zip` from a release and copy the `.sdPlugin`
folder into your Stream Deck plugins directory — see
[`streamdeck/README.md`](streamdeck/README.md).

## Localization

Bundled languages: **English, Italian, Spanish, French, German, Portuguese (BR),
Russian, Chinese (Simplified), Japanese, Korean**. Pick one in Settings or let it
follow OBS's UI language; any other OBS language falls back to English. Strings
live in [`data/locale/`](data/locale/) (`en-US.ini` is the canonical key set).

## Compatibility

| | |
|---|---|
| **OBS Studio** | **31.0+** (CI-certified). Built and tested against **32.1.2**. |
| **Platforms** | Windows x64, Linux x86_64, macOS universal (Intel + Apple Silicon) |
| **Qt** | Qt 6 |

## Building from source

Requires CMake ≥ 3.22, a C++17 compiler, Qt 6, and OBS development files
(`libobs`, `obs-frontend-api`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Unit tests (no OBS/Qt needed):
```bash
cmake -B build-tests -DBUILD_PLUGIN=OFF -DBUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

CI ([`.github/workflows/build_project.yml`](.github/workflows/build_project.yml))
builds OBS dev libraries from source (cached per OBS version) and the plugin per
platform, renders the Stream Deck icons and packages the companion, and runs an
on-demand `compat` matrix against older OBS SDKs. See
[`docs/superpowers/`](docs/superpowers/) for the design spec and plan.

## License

MIT — see [LICENSE](LICENSE).
