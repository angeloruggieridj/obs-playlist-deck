# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.6] — 2026-08-28

### Added

- Release packages ship a `SHA256SUMS.txt` and a signed GitHub build provenance
  attestation, so a download can be tied to the workflow run, commit and runner
  that produced it. When a `VT_API_KEY` secret is configured, each release is
  also scanned by VirusTotal and the reports are linked from the release page.

### Changed

- The README states plainly that the builds are not code-signed, what each
  platform does about that — macOS quarantines the plugin and OBS then fails to
  load it silently — and how to verify a download.

## [1.2.5] — 2026-08-28

### Fixed

- **The update notice showed garbage characters after the version number.** The
  label was built with
  `QStringLiteral("v%1 — <a …>update to %3 \xE2\x86\x97</a>")`. `QStringLiteral`
  expands to a UTF-16 `u""` literal, where a hex escape is a *code unit*, not a
  byte — so the three UTF-8 bytes of the ↗ arrow became U+00E2 U+0086 U+0097: an
  accented "a" followed by two invisible control characters. It was broken on
  every platform; macOS just drew the controls most visibly. Non-ASCII inside a
  `QStringLiteral` is now written as a universal character name (`↗`), which
  the compiler transcodes correctly whatever the source and execution charsets
  are.
- **Windows builds could ship the language names as `?`.** MSVC reads a UTF-8
  source file as the system ANSI code page unless told otherwise and re-encodes
  narrow literals to that page, which cannot represent `Русский`, `简体中文`,
  `日本語` or `한국어`. The build now passes `/utf-8`, pinning both the source and
  the execution charset to UTF-8 — what GCC and Clang already do. Every non-ASCII
  narrow literal also states its encoding through `QString::fromUtf8()` instead
  of relying on the compiler default.

### Changed

- **The playlist, transport and file buttons are icon-only.** Their captions
  moved into the tooltip, so a full row of controls fits a dock narrow enough to
  sit beside the OBS preview. Nothing was un-localized in the process: the
  tooltip is the translated `Tip.*` description, and the translated `Btn.*` name
  now feeds `accessibleName` for screen readers.
- **The update link text is translated.** "update to v1.2.6" was hardcoded
  English inside the literal that carried the encoding bug; it is a new
  `Link.UpdateTo` key in all ten locales.

### Security

- The release tag returned by the GitHub API is HTML-escaped before it reaches
  the rich-text version label.

## [1.2.4] — 2026-08-26

### Fixed

- **The deck can now be shown and hidden from the *Docks* menu, and stays that
  way across restarts.** The dock had no entry in the menu at all, and closing
  it was pointless: it came back visible at the next OBS launch. Two causes:
  - It was registered with `obs_frontend_add_custom_qdock()`, the deliberately
    unmanaged variant — OBS adds no toggle action for it. Registration now goes
    through `obs_frontend_add_dock_by_id()`, which is what puts the checkable
    entry in the *Docks* menu and lets OBS own the dock's state.
  - It was registered on `OBS_FRONTEND_EVENT_FINISHED_LOADING`. OBS restores the
    saved dock layout right after the plugins are loaded and *before* that event,
    so a dock added later never gets its visibility, position or size back.
    Registration now happens in `obs_module_load()`.

### Changed

- `PlaylistDock` is a plain `QWidget`; OBS wraps it in its own dock. Setup that
  needs a scene collection (source binding, session restore, hotkeys, update
  check) moved out of the constructor into a second stage that still runs on
  `OBS_FRONTEND_EVENT_FINISHED_LOADING`.
- Rebuilding the UI after a language change no longer leaks the previous widget
  tree.

## [1.2.3] — 2026-08-18

### Fixed

- **The update check never worked on Windows**, in any version shipped so far.
  `QNetworkAccessManager` needs a Qt TLS backend plugin to speak HTTPS and OBS
  bundles none, so every check died at initialization with "TLS initialization
  failed" before any traffic left the machine — and the error only reached the
  OBS log, making the failure indistinguishable from "up to date". The check now
  uses libcurl, which OBS ships on all three platforms with its own TLS, and runs
  on a background thread.
- A rate-limited GitHub (HTTP 403 with no `tag_name`) is reported as an error
  instead of being read as "no update available".
- Failures are visible: a manual check reports its outcome in the status line.

### Added

- A **Check for updates** button in Settings. The automatic check runs once at
  OBS startup; closing and reopening the dock does not re-run it.

## [1.2.2] — 2026-08-18

### Fixed

- **Switching scene collection could empty the file path of a media source the
  user had configured in OBS.** The dropdown repopulates for the incoming
  collection and `QComboBox` selects index 0 as soon as an item is added to an
  empty box, so a collection without the configured source fell through to
  whichever media source was enumerated first — the deck bound that arbitrary
  source, persisted it over the stored choice, and cleared its `local_file`.
  - The dropdown now always carries an explicit "no source configured" entry at
    index 0 and resolves the selection by item data rather than display text; an
    unmatched name falls back to the placeholder, never to a real source.
  - The remembered choice is updated only by a deliberate user pick, so it
    survives collection switches and returns with the owning collection.
  - Clearing the file at startup is gated on the path belonging to the playlist,
    so a path configured in OBS is never emptied.

### Added

- "No source configured" strings for all 10 shipped locales.

## [1.2.1] — 2026-08-08

### Fixed

- The update check is performed reliably rather than being skipped in some
  startup paths.

## [1.2.0] — 2026-08-08

### Fixed

- The dock releases its strong `obs_source_t` reference before OBS starts
  unloading the outgoing scene collection, instead of holding it through the
  switch.

## [1.1.0] — 2026-06-23

### Added

- Playlist UX: drag & drop from the file manager, drag-reorder, a filter box for
  long playlists, and a visible marker for missing files.
- Playback modes **Shuffle** and **Repeat one**, plus a now-playing progress bar
  with elapsed / total / remaining time.
- Settings dialog with optional auto-restore of the last playlist and background
  duration probing.
- Localized UI in 10 languages, selectable or following OBS.
- obs-websocket vendor API (`Next`, `Previous`, `PlayPause`, `Stop`, `PlayIndex`,
  `Load`, `GetStatus`) and an Elgato **Stream Deck** companion, packaged by CI.
- CI compatibility matrix against older and upcoming OBS SDKs.

### Fixed

- Progress bar and time counter no longer show the previous session's clip when
  no playlist item is loaded.
- The bound source's stale `local_file` is cleared at startup, so taking the
  source to Program on launch no longer replays the clip from before shutdown.
- The Windows package uses the per-user plugin layout, so it extracts into the
  OBS user plugins folder without admin rights and survives OBS updates.
- Reordering while a clip plays keeps the user's selection instead of snapping it
  to the playing item.
- Save/Export honors the chosen file-type filter: picking M3U writes `.m3u`
  instead of always `.json`.

## [1.0.1] — 2026-06-21

### Fixed

- Label visibility in the dock.

## [1.0.0] — 2026-06-21

First public release: a native OBS dock that manages a playlist of local media
files and drives an existing OBS media source from it — transport controls,
end-of-clip modes, save/open playlists as JSON or M3U, global OBS hotkeys, and a
built-in update check.

[1.2.6]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.5...v1.2.6
[1.2.5]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.4...v1.2.5
[1.2.4]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.3...v1.2.4
[1.2.3]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.2...v1.2.3
[1.2.2]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.1...v1.2.2
[1.2.1]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/angeloruggieridj/obs-playlist-deck/releases/tag/v1.0.0
