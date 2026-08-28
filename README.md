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
[![Build provenance](https://img.shields.io/badge/provenance-attested-2da44e?logo=github&logoColor=white)](#unsigned-builds-and-how-to-verify-them)

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
- [Unsigned builds, and how to verify them](#unsigned-builds-and-how-to-verify-them)
- [Usage](#usage)
- [End-of-clip modes](#end-of-clip-modes)
- [Remote control & Stream Deck](#remote-control--stream-deck)
- [Localization](#localization)
- [Compatibility](#compatibility)
- [Building from source](#building-from-source)
- [Changelog](#changelog)
- [License](#license)

## Features

- 🎛️ Native Qt dock inside OBS; bind to any **Media Source** (`ffmpeg_source`)
  or **VLC Source** via a dropdown.
- 📃 Playlist with add / remove / reorder / clear and transport controls; each
  item shows its **duration**. The toolbars are **icon-only** so the dock stays
  narrow enough to sit beside the preview — hover a button for its description.
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

> [!IMPORTANT]
> The builds are **not code-signed**, so your OS may block or warn about them the
> first time — on **macOS** you have to clear the download quarantine by hand or
> OBS will refuse to load the plugin. See
> [Unsigned builds, and how to verify them](#unsigned-builds-and-how-to-verify-them).

### Windows
Extract the zip into OBS's user plugins folder (`%PROGRAMDATA%\obs-studio\plugins`,
i.e. `C:\ProgramData\obs-studio\plugins`), so you get
`C:\ProgramData\obs-studio\plugins\obs-playlist-deck\bin\64bit\obs-playlist-deck.dll`
(and `…\obs-playlist-deck\data\`). Survives OBS updates — no need to touch the
OBS install folder.

```powershell
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

## Unsigned builds, and how to verify them

Playlist Deck is a free, one-person project, and code-signing certificates are
neither free nor issued to projects: an Apple Developer membership is a yearly
fee, and a Windows OV/EV certificate costs more. So the released packages carry
**no publisher signature on any platform**. That is why your system treats them
as untrusted, and what you have to do about it:

| Platform | What you'll see | What to do |
|---|---|---|
| **macOS** | Gatekeeper quarantines anything downloaded from a browser. OBS then fails to load the plugin, usually silently — it simply never appears in the *Docks* menu. | Clear the quarantine attribute once, as shown in the [install step](#macos-universal): `xattr -dr com.apple.quarantine "$PLUGIN_DIR/obs-playlist-deck.plugin"`. The bundle is ad-hoc signed, so nothing else is needed. |
| **Windows** | The downloaded `.zip` is marked as coming from the internet; SmartScreen may warn, and some antivirus products flag unsigned DLLs on sight. | Right-click the zip → **Properties** → tick **Unblock**, then extract. If your antivirus quarantines the DLL, verify it first (below) and add an exclusion. |
| **Linux** | Nothing. There is no signature check to fail. | Just extract it. |

"Unsigned" means nobody paid to vouch for the file — it does not mean the file
is unverified. Every release is built in public by
[GitHub Actions](.github/workflows/build_project.yml) from the tagged source,
and each one ships evidence you can check yourself:

**1. Integrity.** GitHub publishes a SHA-256 digest for every release asset, so
hash your download and compare:

```bash
# macOS / Linux
shasum -a 256 obs-playlist-deck-macos-universal.tar.gz
```
```powershell
# Windows
(Get-FileHash obs-playlist-deck-windows.zip -Algorithm SHA256).Hash
```
```bash
# what GitHub says it should be
gh release view v1.2.6 --repo angeloruggieridj/obs-playlist-deck --json assets \
  --jq '.assets[] | .name + "  " + .digest'
```

This catches a truncated or corrupted download. It is not evidence of who built
the file — the digest and the file come from the same place — which is what the
next step is for.

**2. Build provenance.** Each package is published with a signed
[GitHub attestation](https://docs.github.com/actions/security-guides/using-artifact-attestations)
binding it to the exact workflow run, commit and runner that produced it — proof
it came out of this repository's CI and not off someone's laptop. This is the
guarantee that actually replaces a publisher signature. Verify it with the
[GitHub CLI](https://cli.github.com/):

```bash
gh attestation verify obs-playlist-deck-macos-universal.tar.gz --repo angeloruggieridj/obs-playlist-deck
```

**3. Malware scan.** When a VirusTotal API key is configured for the repository,
each release is scanned across ~70 antivirus engines and the report links are
added to the release page. You can also upload any file to
[virustotal.com](https://www.virustotal.com/gui/home/upload) yourself — a handful
of engines flagging an unsigned DLL is a common false positive, which is exactly
why the provenance attestation matters more than a scan.

> The VirusTotal badge at the top of this page is static: it says the packages
> **are** scanned, not that any particular scan came back clean. VirusTotal has
> no live badge endpoint — the per-file reports linked from each release are the
> actual result, and they are what you should read.

> Provenance attestations are produced from **v1.2.6 onwards**. Earlier releases
> have GitHub's asset digests but no attestation.

Finally, nothing here is a black box: the plugin is MIT-licensed, the full source
is in this repository, and you can always
[build it yourself](#building-from-source).

## Usage

Show or hide the deck from OBS's **Docks** menu — it has a checkable *Playlist
Deck* entry, and OBS remembers whether it was open, where it was docked and how
big it was the next time you start.

1. Add a **Media Source** (or VLC Source) to a scene in OBS.
2. In the Playlist Deck dock, select it from the **Media source** dropdown.
3. **Add** media files (or drag them in), then double-click an item — or select
   it and press **Play** — to play it through that source. Every button shows
   its icon only; hover it to read what it does, in your language.
4. Choose an **End-of-clip** behavior, and use **Save** / **Open** to keep
   playlists as files.

The dropdown lists only the media sources of the **active scene collection**. If
you switch to a collection that has no source by the configured name, the
dropdown shows *“No source configured”* and the deck stays unbound — it never
picks a source for you, and never touches a file path you set up in OBS. Your
choice is remembered, so it comes back when you return to the collection that
owns that source.

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
live in [`data/locale/`](data/locale/) (`en-US.ini` is the canonical key set),
UTF-8 encoded without a BOM — including the button tooltips, which carry the
name of every icon-only control.

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

## Changelog

Release-by-release notes live in [CHANGELOG.md](CHANGELOG.md).

## License

MIT — see [LICENSE](LICENSE).
