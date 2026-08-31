# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.3.2] — 2026-08-31

A fix for a regression 1.3.1 introduced, and the features the deck was missing
to run a show on its own: a library of playlists, watched folders, scheduled
starts, a panic button, and a way to find files that moved.

### Fixed

- **"Load next (paused)" advanced the playlist by two.** Reported against 1.3.1:
  the first clip ended, the operator transitioned Program → Preview, and the
  deck loaded the clip *after* the next one — with the wrong row marked as
  playing, because that is genuinely where playback had gone.
  Handing a media source a new file (`obs_source_update`, a restart, and for a
  staged clip a pause on top) makes it report that the *outgoing* media ended,
  on top of the end that started the sequence. That second end arrived when the
  source was already off air, so it was acted on immediately: item 1 was staged
  and item 2 replaced it before anyone saw item 1.
  An end can only belong to a clip that has started. The engine now ignores one
  that arrives before the source says the clip it was handed is playing, and
  clears the flag either way — a source that never reports a start costs at most
  one missed advance instead of wedging the deck. Four regression tests cover
  the exact sequence, including the plain auto-advance case, which had the same
  hazard without anyone hitting it.

### Added

- **A library of playlists.** A show is rarely one list: a warm-up set, the main
  set, a folder of stingers. The picker at the top of the dock switches between
  them; the button beside it creates, renames, duplicates and deletes them.
  `SwitchPlaylist` and `GetPlaylists` join the remote API, and the library is
  always saved and always restored — there is no "restore on startup" switch any
  more, because a deck that forgot the sets you named would be broken, not
  configurable. A `session.json` from 1.3.x becomes the first playlist on
  upgrade and is left in place, so downgrading loses nothing.
- **Automatic backups of the library**, taken when OBS starts, when it closes,
  and every ten minutes of editing, twenty copies deep.
  **Settings → Restore a backup…** brings one back — and backs up what you
  currently have first, because restoring must not be the operation that loses
  today's work.
- **Watch folder**, per playlist: media files that appear in it are added
  automatically. A file still being copied is given a moment to finish, and a
  file already in the playlist is never added twice.
- **Scheduled start**, per playlist: the deck begins at a wall-clock time you
  set, counting down in the card for the last ten seconds first — the deck
  acting on its own must never be a surprise. A time that has already passed
  starts immediately rather than being ignored, because OBS may simply have been
  closed. It fires once.
- **Panic**: a button, an OBS hotkey, a Stream Deck key and a `Panic` request,
  all of which stop playback and cut to a scene you nominate in Settings.
  Stopping alone is not enough — a stopped media source holds its last frame on
  air. Without a scene configured it still stops, and says why nothing else
  happened.
- **Find moved files** (list context menu): searches the folders your other
  clips live in, the watch folder and the playlist's own folder for a file of
  the same name. One match is repaired, and the repair is undoable; two matches
  are reported rather than guessed at, because picking the wrong `intro.mp4`
  mid-show is worse than saying nothing.
- **Import from an OBS source**: reads the media paths out of any source that
  holds a list of them — a VLC source, OBS's own playlist source, whatever a
  plugin adds. It only reads. The deck drives the two source types whose
  settings it has verified against OBS's own code, and does not write settings
  it has never seen; guessing the third is exactly the mistake that left VLC
  support inert for three releases.
- A playlist that can act on its own says so in the picker (`●` watched,
  `⏱` scheduled).

### Changed

- **The list is a model and a view** (`QAbstractListModel` + `QListView`)
  instead of a `QListWidget`. Every change used to mean clearing the widget and
  rebuilding every row, taking the selection, the scroll position and any open
  editor with them; a batch of durations arriving now updates only the rows that
  changed. Reordering and renaming are requests the dock answers through the
  undo history, and a reorder keeps playing what was playing — found again by
  path, not by the row it used to occupy.
- Playback moving on its own scrolls the list to the clip that started, without
  taking the selection away from wherever the operator put it.
- Adding files skips paths already in the playlist.
- `GetStatus` reports `playlistIndex` and `scheduledStartMs`.
- The locale check now also fails on a translated key that nothing uses: nine
  translations of a string nobody can see are nine translations to maintain for
  nothing. Three such keys were retired.

### Security

- The library file is read with the same care as the session file it replaces: a
  version from the future is set aside rather than half-understood, and one
  damaged playlist inside it costs that playlist, not the library.

## [1.3.1] — 2026-08-31

Audio control in the deck, the last of the remote API, and the structural work
1.3.0 deferred: the decisions about what plays and when are now testable without
OBS.

### Added

- **Mute the bound source from the now-playing card.** The button reads the
  source's own mute and follows it: change it in the OBS audio mixer and the
  deck agrees, because it never keeps a second copy. There is a global hotkey
  for it, a Stream Deck action, and `ToggleMute` / `SetMute` on the remote API.
  Nothing about it is written to the plugin's settings — OBS already saves it
  with the scene collection, and a second opinion would only disagree with the
  first.
- **"Unmute when a clip starts"**, in Settings, **off by default**. Off, the
  mute stays exactly where the operator put it, across clip changes and across
  restarts. On, every clip starts audible — including one that starts while the
  source is on air, which is why it is opt-in rather than the default.
- **Remote API**: `Save {path}`, `Move {from,to}` and `Remove {index}` complete
  the set; `GetStatus` reports `muted`; a `mute-changed` event joins
  `item-started`, `playback-state` and `playlist-completed`.
- **The Stream Deck's *Play Item* action picks the clip by name.** The property
  inspector loads the live playlist over the same connection the Test button
  uses and offers it as a list. Nobody knows what index 7 is at showtime; the
  number stays editable for a playlist that is not loaded yet.
- **Micro-benchmarks** (`playlist-deck-tests -ts=benchmarks -s`) for the JSON and
  M3U round-trips and the playlist edits, at 10 000 items. They log and never
  fail: what they are for is the change that turns a linear path quadratic,
  which is precisely how the per-rebuild `stat()` shipped.
- **`docs/decisions.md`** — why the plugin is built the way it is, including
  what is deliberately *not* done and what would make each decision wrong.

### Changed

- **The end-of-clip state machine moved into `src/core/` as `PlaybackEngine`,**
  behind an `IMediaTransport` interface with five calls. It drives the OBS
  source at runtime and a fake that records calls in the tests, so all six
  modes, the shuffle bag and the staged-clip rule are now covered by 20 real
  test cases instead of by asserting on the text of the dock's source. The
  source-inspection test that pinned the staging rule was deleted, which is what
  those tests are supposed to make possible.
  Along the way the engine fixed a case the dock never handled: a staged clip
  whose row is removed before it loads is dropped, instead of loading whatever
  item inherited that position.
- **Settings and session persistence moved into `SettingsStore`.** No widget
  code touches a file any more, and `PlaylistDock.cpp` is about 300 lines
  lighter despite gaining the audio controls.
- The dock no longer keeps its own copy of the mode, the shuffle bag or the
  pending staged row: there is one owner for each, which is the change that
  makes the whole area testable.
- Windows install instructions now document the **per-user** folder
  (`%APPDATA%\obs-studio\plugins`) alongside the per-machine one. That is the
  layout the release package has always been built for, and it needs no
  administrator rights.
- The undo button has its own glyph instead of the `×` that reads as "close",
  and the orphaned `x.svg` is gone rather than left unused in the resources.

### Security

- **Every GitHub Action is pinned to a commit**, with its tag in a trailing
  comment. A tag is a movable pointer: whoever can push to an action's
  repository can repoint it at different code, and every workflow trusting that
  tag runs it on the next build.
- **The plugin build refuses a Qt it was not meant for.** A Qt plugin loaded
  into OBS runs against *OBS's* Qt; a major.minor mismatch does not fail to
  load, it crashes later somewhere unrelated. CI passes the expected version and
  the build stops if the toolchain drifts.

### Fixed

- Six source files carried no SPDX identifier, and five of the
  source-inspection test files did not say in the file what kind of test they
  were or when to replace one.

## [1.3.0] — 2026-08-31

The release that makes the deck do what it always said it did — VLC sources
included — and rebuilds the dock around what an operator needs to read at a
glance during a show.

### Fixed

- **Binding a VLC source did nothing at all.** The controller drove both source
  types by writing `is_local_file` and `local_file`, but OBS's VLC source has
  neither setting: it reads a `playlist` array of `{ "value": "<path>" }`
  objects. Every write was accepted and ignored, so selecting a playlist item
  never changed what a bound VLC source played, `currentFile()` always read back
  empty (which is why the stale-file cleanup at startup had nothing to compare
  against), and `media_ended` only ever fired when VLC's own internal playlist
  ran out. The README has promised VLC support since 1.0. The controller now
  records the source kind at bind time and writes whichever setting that source
  actually reads — and, for VLC, also turns its `loop` off, because a looping
  playlist never reaches its end and auto-advance would have stayed dead
  otherwise.
- **The next clip was never staged in the most common Studio Mode setup.**
  "Load next (paused)" hung on the source's `deactivate` signal, which OBS raises
  only when a source is in no active scene at all — and in Studio Mode a source
  sitting in the preview scene still counts as active. The ordinary
  Program → Preview transition therefore raised nothing: the staged clip was
  never loaded, and then loaded itself unprompted, possibly hours later, when the
  source finally went inactive. The trigger is now the actual condition — the
  bound source is no longer in the Program scene — computed from the frontend's
  scene, preview-scene and studio-mode events, with `deactivate` kept only as the
  fallback for a source removed from every scene. While a clip is staged the
  now-playing card says so, so the deck never holds a surprise.
- **`GetStatus` read the playlist from the websocket thread.** The other vendor
  requests marshalled to the UI thread; this one called straight into the model
  while the UI thread could be adding to or erasing from the same `std::vector`.
  A read landing inside an `erase` is undefined behaviour — the kind of crash
  that only happens during a busy show and never reproduces afterwards. The dock
  now publishes an immutable snapshot under a mutex, and the websocket thread
  reads only that.
- **Two background threads were detached and unowned.** Duration probing and the
  update check each ran on a `std::thread(...).detach()`. A detached thread
  cannot be cancelled, cannot be waited for, and outlives the objects it posts
  to: at OBS shutdown one could still be calling into a Qt application that was
  being torn down. Both now live on threads the dock owns and joins during
  shutdown, and a probe whose playlist has since been replaced is abandoned
  mid-flight instead of delivering answers about a list that no longer exists.
- **A crash mid-write destroyed the saved state.** `settings.json`, `session.json`
  and saved playlists were written by truncating the file and writing over it. An
  interruption left truncated JSON, which does not parse — so the next start
  silently lost the whole playlist. Every write is atomic now (write a temporary,
  rename it into place). The session was also rewritten on *every* rebuild — once
  per added file, per probed duration, per reorder — and is now debounced to at
  most one write per 800 ms, flushed on exit.
- **The dock froze on every click with a playlist on a network share.** Rebuilding
  the list ran a synchronous `QFileInfo::exists()` on every item, and the rebuild
  runs after every add, remove, move, reorder, probe result and language change.
  Combined with per-file probe results, dropping *n* files cost O(n²) stat calls.
  Existence is now checked once on a worker thread, cached, and delivered in
  batches with the durations.
- **A duration could be written onto the wrong item.** It was read 700 ms after
  playback started, on a fixed timer that had no idea which item it belonged to:
  pressing Next inside that window wrote the new clip's duration onto the previous
  item. The read is now triggered by the source reporting that it started, and is
  discarded unless the item it was scheduled for is still the current one. The
  same signal replaces the pair of guessed timers that paused a staged clip —
  they assumed the decoder was ready within 400 ms, and a heavy or remote file
  went on air instead of staying paused.
- **Removing the item that was playing made the next auto-advance skip one.**
  `current` was slid onto whatever item inherited that row, so the model believed
  a different clip was playing. Removing the current item now clears the current
  index and stops playback, with a status line that says so.
- **A single damaged entry threw away the whole playlist file.** A JSON playlist
  with 200 good items and one malformed one was rejected outright. Bad entries
  are skipped and counted now ("Opened, 1 damaged item skipped"); only a file
  that is not a playlist at all is refused.
- **`.m3u` files from other players showed every item as missing.** Their paths
  are relative to the playlist file — the portable layout every other player
  writes — and were read as absolute. Relative paths in both `.m3u` and `.json`
  are now resolved against the playlist's own folder.
- **Durations written as decimals by other tools were dropped** rather than
  rounded.
- **Shuffle was not a shuffle.** It drew uniformly at random each step, so items
  repeated long before the list was exhausted, and because a draw equal to the
  current item was nudged to the next index, the item after the current one had
  roughly twice everyone else's probability. It is a bag shuffle now: every item
  plays once, in random order, before any repeats.
- **The buttons were unreachable by keyboard.** Every one carried an
  `accessibleName` and `Qt::NoFocus` at the same time — named for a screen
  reader, unreachable by the keyboard that drives one. Focus policy is back to
  the default, with a visible focus ring.
- **Icons did not follow a theme switch.** They were tinted once from the palette
  in force when the dock was built; OBS 31 can change theme while running, and
  they stayed dark on dark. The dock now re-tints on a palette change, and the
  one hardcoded colour in the project (`#e06c75`, which served as both "file
  missing" and "error" at about 3.9:1 contrast) is gone in favour of semantic
  tokens derived from the live palette.
- **The refresh timer ran forever with the dock closed**, polling the bound
  source and repainting widgets nobody could see. It stops when the dock is
  hidden.
- **Four hotkey targets were allocated and never freed**, and are now owned by
  the dock and released only after the hotkeys themselves are unregistered.
- **The session restored the playlist but not which file it came from**, so the
  label claimed nothing was loaded.
- **`tools/gen_locales.py` was four releases out of date.** Its hardcoded key
  list stopped at 1.2.1, so running it — to add a language, say — would have
  silently deleted every string added since: a tool in the repository that
  introduced a regression when used. Locale files are now generated from
  `tools/locales.json`, the single source of truth, and CI fails if the shipped
  files, the key sets or the `%1` placeholders drift apart.
- **Stream Deck: presses made while OBS was away fired all at once later.** The
  companion queued them and replayed the queue on the next successful connection
  — seven `Next` actions in a row, live, minutes after the fact. They are dropped
  now, and the connection retries on its own with backoff instead of staying dead
  until the app is restarted.
- **Stream Deck: the property inspector lost what you typed.** Fields saved on
  `change` only, so a password confirmed with Enter, or typed just before the
  panel closed, was never stored.

### Added

- **A now-playing card at the top of the dock**: title, elapsed / total /
  remaining on tabular figures, a **seekable** progress bar, the transport, and
  what plays next — the whole live picture in one block, instead of spread across
  three widgets in three parts of the dock. Play/Pause finally shows which of the
  two it will do.
- **Rename an item** (F2, the pencil button, or the context menu) with "reset
  name from file" to undo it later. The title override is saved in `.json` and
  exported as the `#EXTINF` title in `.m3u`.
- **Undo and redo** (Ctrl+Z / Ctrl+Shift+Z) for every playlist edit: add, remove,
  clear, move, reorder, rename, open. Clear no longer asks for confirmation,
  because an undo that works is worth more than a prompt that gets clicked
  through. Playback is deliberately not undoable.
- **Multi-selection** and bulk remove.
- **Add a whole folder** (recursive), sorted the way people read numbers —
  `clip2` before `clip10` — and **export the playlist as CSV**.
- **Portable playlists**: an option to save with paths relative to the playlist's
  own folder, so a gig folder with its media can move to another machine.
- **An empty state with a drop affordance.** Dropping files onto an empty list
  always worked; nothing on screen said so.
- **Item count and total running time** under the list, and a match count in the
  filter box.
- **Keyboard operation**: Enter plays, Delete removes, F2 renames, Ctrl+F focuses
  the filter, and Tab reaches everything.
- **More hotkeys**: recheck missing files, and play item 1-9 — what a MIDI
  controller or foot pedal maps to.
- **Recheck missing files** on demand, from the context menu, instead of
  restarting OBS.
- **Vendor API v2**, purely additive: `GetStatus` gains `currentTitle`,
  `currentPath`, `positionMs`, `durationMs`, `playing`, `paused`, `sourceBound`,
  `sourceName`, `mode`, `modeName`, `playlistName`, `upNextIndex`, `upNextTitle`
  and `pluginVersion`, and there are new `GetItems`, `SetMode`, `Seek`,
  `AddPaths` and `Clear` requests. The v1 fields and requests are untouched, so
  existing scripts keep working.
- **Vendor events**, so a remote client can follow playback instead of polling:
  `item-started`, `playback-state` (about once a second) and `playlist-completed`.
  The header has always exposed the emit call; nothing used it until now.
- **Stream Deck keys show state**: the current clip on Play/Pause, what comes
  next on Next, and `offline` when OBS cannot be reached. The property inspector
  gained a **Test connection** button that tells apart a wrong password from a
  missing OBS plugin.
- `SECURITY.md`, `CONTRIBUTING.md`, issue templates, a `.clang-format`, and
  `docs/verification.md` — the eighty lines about unsigned builds moved out of
  the README, which is an entry point, into a page of its own.

### Changed

- **The dock was rebuilt around the list.** Three bold section headers are gone,
  giving their rows back to the playlist; the operations toolbar sits under the
  list it edits; the end-of-clip mode and the save/open buttons share one footer
  row; the status line clears itself after a few seconds and distinguishes
  information from warnings and errors, which the previous single colour could
  not. A missing file is now amber, with words as well as colour, and reserved
  red is kept for things that actually failed.
- **The end-of-clip mode is chosen from a table, not from the combo box's row
  index.** The old mapping worked only because the menu order matched the enum:
  reordering the menu would silently have made "Stop" run a shuffle.
- The end-of-clip state machine, the shuffle bag, the undo history, the staging
  rule, the source-kind mapping and the path handling moved into `src/core/`,
  which has no OBS and no Qt, so all of it is unit-tested directly. The update
  check and the file scanner moved out of the dock into classes of their own.
- The dock's icons are cached per resource and ink colour rather than rasterized
  on every use, and the tick no longer copies the whole playlist twice a second
  to keep the remote snapshot current.
- Four icons that shipped unused (`pencil`, `trash`, `download`, `upload`) are
  used now — rename, clear, add folder, export — and two were added (`search`,
  `music`).
- The Stream Deck companion's version follows the plugin's (it read 1.1.0 against
  a 1.2.6 plugin), the macOS bundle version is read from the build instead of
  being typed in, and CI checks that CMake, the Stream Deck manifest and the
  CHANGELOG all name the same version.

### Security

- **The update check's curl handle is hardened.** `CURLOPT_NOSIGNAL` is set —
  without it libcurl resolves DNS timeouts with `SIGALRM` and `siglongjmp`, which
  is undefined behaviour off the main thread and has crashed multithreaded
  programs for as long as curl has existed. `FOLLOWLOCATION` is now bounded by
  `MAXREDIRS`, restricted to HTTPS on redirects, and given a connect timeout; the
  response body is capped, so a redirect to something else entirely cannot grow
  it without limit.
- **A playlist opened by the remote `Load` request is size-capped at 10 MB.** Any
  authenticated obs-websocket client can name a path, and the file's contents are
  then shown in the dock; reading an arbitrarily large file whole would freeze
  OBS. The cap applies to the UI's own Open as well. `SECURITY.md` documents the
  remote surface plainly.
- **CI runs with least privilege.** `contents: write` was granted at the workflow
  level and inherited by every build job; only the release job asks for it now.

## [1.2.6] — 2026-08-28

### Added

- Release packages carry a signed GitHub build provenance attestation, so a
  download can be tied to the workflow run, commit and runner that produced it —
  the guarantee an unsigned build otherwise lacks. When a `VT_API_KEY` secret is
  configured, each release is also scanned by VirusTotal and the reports are
  linked from the release page. No `SHA256SUMS` file is published: GitHub already
  exposes a sha256 digest per asset, from the same source, and the attestation
  covers each asset directly rather than through a checksum file.

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

[1.3.2]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/angeloruggieridj/obs-playlist-deck/compare/v1.2.6...v1.3.0
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
