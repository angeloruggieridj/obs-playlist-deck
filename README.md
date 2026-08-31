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

[![VirusTotal](https://img.shields.io/badge/VirusTotal-scanned-394eff?logo=virustotal&logoColor=white)](https://github.com/angeloruggieridj/obs-playlist-deck/releases/latest)
[![Build provenance](https://img.shields.io/badge/provenance-attested-2da44e?logo=github&logoColor=white)](docs/verification.md)

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
- [Unsigned builds](#unsigned-builds)
- [Usage](#usage)
- [Playlists, watch folders and scheduled starts](#playlists-watch-folders-and-scheduled-starts)
- [Keyboard](#keyboard)
- [End-of-clip modes](#end-of-clip-modes)
- [Remote control & Stream Deck](#remote-control--stream-deck)
- [Localization](#localization)
- [Compatibility](#compatibility)
- [Building from source](#building-from-source)
- [Changelog](#changelog)
- [License](#license)

## Features

- 🎛️ Native Qt dock inside OBS; bind to any **Media Source** (`ffmpeg_source`)
  or **VLC Source** via a dropdown — each is driven through the settings it
  actually reads, so both really work.
- ▶️ **Now-playing card**: title, elapsed / total / remaining, a **seekable**
  progress bar, transport, and what plays next — the whole live picture in one
  block at the top of the dock.
- 📃 Playlist with add / remove / reorder / **rename** / clear, **multi-select**
  and **undo** (Ctrl+Z); each item shows its **duration**, and the toolbar shows
  the **item count and total running time**.
- 🖱️ **Drag & drop** files from the OS file manager; reorder by drag; missing
  files flagged; a **filter** box with a match count for long playlists.
- 📂 Add a **whole folder** (recursive, sorted the way people read numbers:
  `clip2` before `clip10`), and export the playlist as **CSV**.
- 🔀 Playback modes: **Play next**, **Loop**, **Load next (paused)**, **Stop**,
  **Shuffle** (a real bag shuffle — every clip plays before any repeats),
  **Repeat one**.
- 💾 Save / open playlists as **`.json`** or **`.m3u/.m3u8`**, with **relative
  paths** on request so a gig folder can move between machines; `.m3u` files
  written by other players are read correctly. Optional **auto-restore** of the
  last playlist; **background** duration probing.
- 📚 **A library of named playlists** in one deck — a warm-up set, the main set,
  a folder of stingers — switched from a dropdown and kept between sessions,
  with automatic **backups** you can restore from.
- 👀 **Watch a folder**: media dropped into it joins the playlist by itself.
- ⏰ **Scheduled start**: a playlist can begin at a wall-clock time, counting
  down in the card first so it is never a surprise.
- 🚨 **Panic**: one button (and hotkey, and Stream Deck key) stops playback and
  cuts to a scene you nominate — a stopped media source holds its last frame,
  so stopping alone is not enough.
- 🩹 **Find moved files**: when files are reorganised between shows, the deck
  looks for them by name where its other files live.
- 🔇 **Mute** the bound source from the card, with an optional "unmute when a
  clip starts" — off by default, so the mute stays where you put it.
- ⌨️ Global OBS **hotkeys** (next, previous, play/pause, stop, mute, recheck
  files, play item 1-9) and full **keyboard operation** of the dock itself.
- 🕹️ **Remote control** via obs-websocket — requests *and* live events — plus an
  included **Stream Deck** companion that reconnects on its own and shows the
  current clip on the key.
- 🌍 **Localized** UI (10 languages) — selectable or follow OBS.
- 🔔 Built-in update check (links to the latest release; manual download).

## Installation

Download your platform's build from the
[**Releases**](https://github.com/angeloruggieridj/obs-playlist-deck/releases) page.

> [!IMPORTANT]
> The builds are **not code-signed**, so your OS may block or warn about them the
> first time — on **macOS** you have to clear the download quarantine by hand or
> OBS will refuse to load the plugin. See
> [Unsigned builds, and how to verify them](#unsigned-builds-and-how-to-verify-them).

### Windows
Extract the zip into an OBS plugins folder, so you end up with
`…\plugins\obs-playlist-deck\bin\64bit\obs-playlist-deck.dll` (and
`…\obs-playlist-deck\data\`). Either location works and both survive OBS
updates — no need to touch the OBS install folder.

```powershell
# Per user (recommended): no administrator rights needed
Expand-Archive obs-playlist-deck-windows.zip -DestinationPath "$env:APPDATA\obs-studio\plugins"

# Per machine: every account on the PC, may prompt for elevation
Expand-Archive obs-playlist-deck-windows.zip -DestinationPath "$env:PROGRAMDATA\obs-studio\plugins"
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

## Unsigned builds

The releases carry **no publisher signature on any platform** — code-signing
certificates are neither free nor issued to one-person projects. Your OS will
say so: macOS quarantines the plugin (and OBS then fails to load it, silently),
Windows marks the zip as coming from the internet.

Unsigned does not mean unverifiable. Every release is built in public from the
tagged source, and ships a signed **build provenance attestation**, GitHub's
per-asset **SHA-256 digests** and, when a key is configured, **VirusTotal**
reports.

👉 **[How to verify a download](docs/verification.md)** — what to expect per
platform, and the three checks, with commands.

## Usage

Show or hide the deck from OBS's **Docks** menu — it has a checkable *Playlist
Deck* entry, and OBS remembers whether it was open, where it was docked and how
big it was the next time you start.

1. Add a **Media Source** (or VLC Source) to a scene in OBS.
2. In the Playlist Deck dock, select it from the **Media source** dropdown.
3. **Add** media files (or drag them in, or add a whole folder from the
   right-click menu), then double-click an item — or select it and press
   **Play** — to play it through that source. Every button shows its icon only;
   hover it to read what it does, in your language.
4. Choose an **End-of-clip** behavior, and use **Save** / **Open** to keep
   playlists as files.

Right-click the list for rename, "reset name from file", add folder, export CSV,
recheck missing files, and undo/redo. A destructive edit — remove, clear, an
accidental reorder — is undone with **Ctrl+Z**: there is no confirmation dialog
to dismiss, because an undo that works is worth more than a prompt that gets
clicked through.

Missing files are marked in amber with the words *file not found*, not by colour
alone. The check runs on a worker thread, so a playlist on a network share does
not freeze the dock; **Recheck missing files** runs it again on demand.

The dropdown lists only the media sources of the **active scene collection**. If
you switch to a collection that has no source by the configured name, the
dropdown shows *“No source configured”* and the deck stays unbound — it never
picks a source for you, and never touches a file path you set up in OBS. Your
choice is remembered, so it comes back when you return to the collection that
owns that source.

## Playlists, watch folders and scheduled starts

The **playlist picker** at the top of the dock holds as many playlists as you
like. The button beside it creates, renames, duplicates and deletes them, and
opens **Playlist properties**, where two things belong to that playlist alone:

| Property | What it does |
|---|---|
| **Watch folder** | Media files that appear in this folder are added to this playlist automatically. Files still being copied are given a moment to finish; a file already in the playlist is not added twice. |
| **Start at** | The deck starts this playlist by itself at that wall-clock time. The card counts down to it for the last ten seconds, and a time that has already passed starts immediately rather than being ignored. |

A playlist with either of these shows a mark in the picker (`●` watched,
`⏱` scheduled), because a list that can act on its own should say so where you
choose it.

The library is **always saved and always restored** — a deck that forgot the
sets you named would be broken, not configurable. It is copied aside when OBS
starts, when it closes, and every ten minutes of editing; the last twenty copies
are kept and **Settings → Restore a backup…** brings one back (backing up what
you have first). A `session.json` from 1.3.x becomes the library's first
playlist on upgrade, and is left in place so downgrading loses nothing.

**Panic** stops playback and cuts to the scene named in Settings. Without a
scene configured it still stops, and says so. It is on a button, an OBS hotkey,
a Stream Deck key and the remote API, because that is a thing you press without
looking.

**Find moved files** (list context menu) searches the folders your other clips
live in, the watch folder and the playlist's own folder for a file with the same
name. One match is repaired; two are reported rather than guessed at, because
picking the wrong `intro.mp4` mid-show is worse than saying nothing.

**Import from an OBS source** reads the media paths out of any source that holds
a list of them — a VLC source, OBS's own playlist source, whatever a plugin
adds. It only reads: the deck drives the two source types whose settings it
actually knows, and does not write settings it has never seen.

## Keyboard

The dock is fully operable without a mouse — every control is reachable by Tab,
and the list carries the shortcuts you would expect.

| Key | In the playlist |
|---|---|
| **Enter** | Play the selected item |
| **Delete** | Remove the selection |
| **F2** | Rename the selected item |
| **Ctrl+F** | Jump to the filter box |
| **Ctrl+Z** / **Ctrl+Shift+Z** | Undo / redo the last playlist change |

Global OBS hotkeys (assign them in OBS → Settings → Hotkeys) cover **Next**,
**Previous**, **Play/Pause**, **Stop**, **Panic**, **Mute/unmute**, **Recheck
missing files** and **Play item 1-9** — the last of these is what a MIDI controller or
foot pedal maps to.

## End-of-clip modes

| Mode | Behavior |
|------|----------|
| **Play next** | Auto-advance to the next item. |
| **Loop** | Auto-advance and wrap around. |
| **Load next (paused)** | Hold the finished clip's last frame on Program; load the next clip, paused, as soon as the bound source is **no longer in the Program scene** — a studio-mode transition, a scene change, or the source being taken off air. Until then the card says a clip is staged. The next clip never goes live early and the playlist never auto-advances on air. |
| **Stop** | Stop at the end of the clip. |
| **Shuffle** | Bag shuffle: every item plays once, in random order, before any repeats. |
| **Repeat one** | Replay the current item. |

## Remote control & Stream Deck

Playlist Deck registers an obs-websocket **vendor** named `obs-playlist-deck`.
Call its requests via obs-websocket v5 `CallVendorRequest` from any client or
script.

| Request | Data | Does |
|---|---|---|
| `Next` / `Previous` / `PlayPause` / `Stop` | — | Transport |
| `ToggleMute` | — | Mute or unmute the bound source |
| `SetMute` | `{ muted }` | Set the mute explicitly |
| `PlayIndex` | `{ index }` | Play an item by position (0-based) |
| `Seek` | `{ positionMs }` | Jump inside the current clip |
| `SetMode` | `{ mode }` | End-of-clip mode, `0`-`5` in the order of the table above |
| `Load` | `{ path }` | Open a playlist file (10 MB cap) |
| `Save` | `{ path }` | Write the playlist (format from the extension) |
| `Move` | `{ from, to }` | Reorder one item |
| `Remove` | `{ index }` | Remove one item |
| `Panic` | — | Stop and cut to the panic scene |
| `SwitchPlaylist` | `{ name }` | Make another playlist in the library active |
| `GetPlaylists` | — | The library's playlist names and which is active |
| `AddPaths` | `{ paths: [{ value }] }` | Append media files |
| `Clear` | — | Empty the playlist |
| `GetStatus` | — | See below |
| `GetItems` | `{ from, to }` | Titles and paths, paginated |

`GetStatus` answers with `ok`, `count`, `currentIndex` — the fields it has always
had — plus `currentTitle`, `currentPath`, `positionMs`, `durationMs`, `playing`,
`paused`, `muted`, `sourceBound`, `sourceName`, `mode`, `modeName`,
`playlistName`, `playlistIndex`, `scheduledStartMs`, `upNextIndex`,
`upNextTitle` and `pluginVersion`. Nothing was removed, so existing scripts keep
working.

The deck also **emits events**, so a client can follow playback instead of
polling: `item-started` (`index`, `title`, `path`, `durationMs`),
`playback-state` (`playing`, `positionMs`, `durationMs`, `index`, about once a
second), `mute-changed` (`muted`) and `playlist-completed`.

An Elgato **Stream Deck companion** lives in [`streamdeck/`](streamdeck/) with
Next / Previous / Play-Pause / Stop / Mute / Panic / Play Item actions (buildless JS). It
reconnects on its own with backoff, drops rather than replays presses made while
OBS was away, and shows the current clip — or `offline` — on the key. The
property inspector has a **Test connection** button that tells apart a wrong
password from a missing OBS plugin, and picks the clip for *Play Item* **by
name** from the live playlist instead of asking for an index. Grab `obs-playlist-deck-streamdeck.zip` from
a release and copy the `.sdPlugin` folder into your Stream Deck plugins
directory — see [`streamdeck/README.md`](streamdeck/README.md).

## Localization

Bundled languages: **English, Italian, Spanish, French, German, Portuguese (BR),
Russian, Chinese (Simplified), Japanese, Korean**. Pick one in Settings or let it
follow OBS's UI language; any other OBS language falls back to English.

Every user-visible string is translated — status messages and file-dialog titles
included. The shipped [`data/locale/`](data/locale/) files are **generated** from
[`tools/locales.json`](tools/locales.json), which is the one file to edit:

```bash
python tools/gen_locales.py           # rewrite data/locale/*.ini
python tools/gen_locales.py --check   # what CI runs
```

`--check` fails if a language is missing a key, if a translation dropped a `%1`
placeholder, if a file carries a BOM, or if a `.ini` was edited by hand. To add
or fix a translation, change `locales.json`, run the generator, and open a PR.

## Compatibility

<!-- obs-compat:start -->
| | |
|---|---|
| **OBS Studio** | **31.0+** (CI-certified). Built and tested against **32.1.2**. |
| **Platforms** | Windows x64, Linux x86_64, macOS universal (Intel + Apple Silicon) |
| **Qt** | Qt 6 |
<!-- obs-compat:end -->

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
runs the unit tests plus the locale and version checks, builds OBS dev libraries
from source (cached per OBS version) and the plugin per platform, renders the
Stream Deck icons and packages the companion, and runs an on-demand `compat`
matrix against older OBS SDKs. See [`docs/superpowers/`](docs/superpowers/) for
the design spec and plan, [docs/decisions.md](docs/decisions.md) for why the
plugin is built the way it is, and [CONTRIBUTING.md](CONTRIBUTING.md) for the
working agreement (one finding, one PR, one CHANGELOG entry).

The `src/core/` library is plain C++17 with no OBS and no Qt, which is what
makes the playlist model, the playlist formats, the playback engine, the shuffle
bag, the undo history, the playlist library, the schedule rules, the moved-file
search and the path handling unit-testable on their own. The engine drives an
`IMediaTransport`: a fake in the tests, the OBS source at runtime.
`src/plugin/` is the OBS and Qt layer on top of it — the dock, a
`QAbstractListModel` for the list, the media source controller, the settings
store and the worker threads.

## Security

Reporting a vulnerability: see [SECURITY.md](SECURITY.md).

## Changelog

Release-by-release notes live in [CHANGELOG.md](CHANGELOG.md).

## License

MIT — see [LICENSE](LICENSE).
