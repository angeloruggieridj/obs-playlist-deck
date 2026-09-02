# Architecture decisions

Why the plugin is built the way it is. Most of these were paid for with a bug;
the reasoning lives in the code as comments, and here so it can be found without
knowing which file to open.

Each entry: what was decided, what it rules out, and what would make it wrong.

---

## 1. The dock is registered from `obs_module_load()`, not from `FINISHED_LOADING`

OBS restores its saved dock layout immediately after modules load and *before*
the `FINISHED_LOADING` event. A dock added later is never given back its
visibility, position or size — it reappears floating, or not at all.

`obs_frontend_add_dock_by_id()` is also what adds the checkable entry to the
Docks menu. The custom-qdock API adds neither, leaving a dock the user cannot
toggle.

Everything that needs a scene collection is deferred to `frontendLoaded()`
instead, which runs on `FINISHED_LOADING`.

**Would be wrong if:** OBS changed when it restores dock state. Pinned by
`tests/test_dock_registration.cpp`.

## 2. The update check speaks HTTPS through libcurl, not Qt Network

`QNetworkAccessManager` needs a Qt TLS backend plugin. OBS ships none on
Windows, so every HTTPS request there dies with "TLS initialization failed" —
the update check was silently dead on the platform most users are on. libcurl is
bundled with OBS on all three platforms and carries its own TLS.

**Cost:** manual option hardening (`NOSIGNAL`, bounded redirects, HTTPS only,
timeouts, a capped response body) that Qt would have handled.

**Would be wrong if:** OBS started shipping the Qt TLS plugin everywhere.

## 3. `src/core/` has no OBS and no Qt

The playlist model, the playlist formats, path handling, the end-of-clip state
machine, the shuffle bag and the undo history are plain C++17. They build and
test in seconds without OBS, and that is the difference between a decision that
is verified and one that is hoped for.

The rule that follows: **if logic can live in the core, it does.** The
`IMediaTransport` interface exists for exactly this reason — the playback engine
drives a fake in the tests and the real OBS source at runtime.

**Cost:** an interface and a small amount of translation at the boundary.

## 4. Remote status is a published snapshot, not the live model

obs-websocket vendor callbacks run on the websocket thread. Reading the playlist
from there while the UI thread mutates the same `std::vector` is a data race,
and a read landing inside an `erase` is undefined behaviour.

Everything that mutates is marshalled to the UI thread with a queued connection;
everything that reads reads a `DeckStatus` snapshot published under a mutex.

**Why not atomics:** the v2 status carries strings (title, path, mode name),
which atomics cannot hold.

**Why not a blocking queued connection:** it would deadlock the moment the UI
thread was itself waiting on something.

## 5. Background work runs on threads the dock owns

A detached thread cannot be cancelled, cannot be waited for, and outlives the
objects it posts to — at OBS shutdown it could still be calling into a Qt
application being torn down. The file scanner and the update checker each own a
`QThread` that `shutdown()` quits and waits for.

A scan carries a generation number, so results about a playlist the user has
already replaced are dropped rather than applied.

## 6. Locale files are generated, never edited

`tools/locales.json` is the single source of truth; `data/locale/*.ini` are
output. The previous generator carried its own copy of the key list, four
releases out of date: running it would have deleted every string added since.

CI runs `gen_locales.py --check`, which fails on a missing key, a dropped `%1`
placeholder, a BOM, or a hand-edited `.ini`.

## 7. Non-ASCII in a `QStringLiteral` is written as a universal character name

`QStringLiteral` expands to a UTF-16 `u""` literal, where a hex escape is a
*code unit*, not a byte: `"\xE2\x86\x97"` is three characters, not one arrow.
This shipped once, visibly, in 1.2.4. Raw non-ASCII is no better — it depends on
the compiler's source charset, which MSVC gets wrong by default (hence `/utf-8`
in the build).

Pinned by a `static_assert` and a test in `tests/test_ui_text.cpp`.

## 8. Source-inspection tests are a bridge, not the destination

Several tests read the plugin sources as text and assert on what they find.
They exist because the behaviour they protect needs a running OBS, and the
regressions are quiet and expensive.

Each carries the bug it prevents in a comment. When logic moves into the core
and gets a real unit test, the inspection test it replaces is deleted — that
happened to the staging rule in 1.3.1, which now lives in
`tests/test_playback_engine.cpp`.

## 9. The mute belongs to the OBS source

The deck reads `obs_source_muted()` and follows the source's `mute` signal
rather than remembering a state of its own. OBS already persists it with the
scene collection, and the audio mixer can change it at any time; a second copy
would disagree with the first the moment either was touched.

Un-muting on clip start is opt-in for the same reason a live tool does not
change what is audible without being asked.

## 10. Playlist state is written atomically, and the session is debounced

A truncate-then-write leaves a half-written file if anything interrupts it, and
truncated JSON does not parse — the whole saved playlist would be gone at the
next start. Everything goes through `SettingsStore::writeAtomically()`
(`QSaveFile`: write a temporary, rename into place).

The session used to be rewritten on every rebuild — once per added file, per
probed duration, per reorder. It is debounced to at most one write per 800 ms,
flushed on exit.

## 11. The library is content, not a preference

Named playlists, their watch folders and their scheduled starts are always
saved and always restored. There is no "restore on startup" switch any more: a
deck that forgot the sets someone named would be broken, not configurable.

That raises the stakes on the file, so it is copied aside when OBS starts, when
it closes, and every ten minutes of editing, twenty copies deep. Restoring one
backs up what is currently there first — restoring must not be the operation
that loses today's work.

## 12. An end that arrives before a start is not that clip's end

Handing a media source a new file makes it report that the *outgoing* media
ended, on top of the end that prompted the change. Acted on, that advanced the
playlist twice (1.3.1). The engine ignores an end until the source says the clip
it was given has started; the flag clears either way, so a source that never
reports a start costs at most one missed advance rather than wedging the deck.

## 13. The list is a model, and edits are requests

The view no longer owns the data. Reordering and renaming are signals the dock
answers, applying them through the undo history and handing back rows — a model
that mutated the playlist directly would have to reimplement undo, or lose it.
`dropMimeData()` deliberately returns `false` after emitting its request: `true`
would have the view delete the source rows itself, on top of the reorder the
dock is about to perform.

## 14. What is deliberately *not* done

- **Driving a source whose settings are unknown.** The deck reads media paths
  out of any source that holds them, and imports those. It writes only the two
  settings shapes it has verified against OBS's own source code. Guessing the
  third is precisely the mistake that left VLC support inert for three releases.
- **A `clang-format` gate in CI.** The config is in the repository; the gate is
  not. Several tests read the sources as text, so a wholesale reformat would
  fail tests unrelated to the change.
- **Code signing.** Certificates are not free and are not issued to one-person
  projects. Build provenance attestation, per-asset digests and VirusTotal
  reports are what ships instead — see [verification.md](verification.md).

## 15. OBS 30.0.0 builds in an ubuntu:22.04 container (2026-08-31)

`ubuntu-24.04` ships FFmpeg 7, which OBS 30-era code does not compile
against. That is an OBS build problem, not a plugin incompatibility, but the
compat matrix could not tell the two apart, so the declared minimum sat at
31.0 while the plugin's actual API floor is 30.0
(`obs_frontend_add_dock_by_id`).

Probing 30.0.0 in an `ubuntu:22.04` container (FFmpeg 4.4, Qt 6.2):
**obs-frontend-api builds and installs.** `libobs.so.30` and
`libobs-frontend-api.so.30` compile cleanly with jammy's g++ 11.4.0 against
Qt6 6.2.4+dfsg-2ubuntu1.1 and libavcodec 7:4.4.2-0ubuntu0.22.04.1, and
`cmake --install` places a complete dev tree — `obs-frontend-api.h` included
— under the install prefix.

Two build-script issues surfaced along the way, neither of which bears on
buildability itself:
- Jammy packages the Qt6 SVG module as `libqt6svg6-dev`; the `qt6-svg-dev`
  metapackage name is a later-Ubuntu convention and does not exist in jammy.
- OBS 30.x's `export_target()` (`cmake/Modules/ObsHelpers.cmake`) tags every
  `install()` rule `COMPONENT obs_libraries`. `--component Development` —
  correct for 31.x+, and already what `build_project.yml`'s `compat` job uses
  for 31.0.0/32.x — silently installs nothing against 30.x: no error, no
  files, just an empty prefix. `--component obs_libraries` is required for
  a 30.x build.

Confirmed apt package list for `ubuntu:22.04` (identical to the brief's list
except `qt6-svg-dev` → `libqt6svg6-dev`):
```
git cmake ninja-build pkg-config extra-cmake-modules g++
qt6-base-dev qt6-base-private-dev libqt6svg6-dev
libavcodec-dev libavformat-dev libavutil-dev libavdevice-dev
libavfilter-dev libswscale-dev libswresample-dev libx264-dev
libcurl4-openssl-dev libjansson-dev libmbedtls-dev
uthash-dev nlohmann-json3-dev zlib1g-dev libpng-dev
libpipewire-0.3-dev libwayland-dev libxkbcommon-dev libgl1-mesa-dev
libgles2-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev
libxcb-xinerama0-dev libxcb-randr0-dev libxcb-shm0-dev
libxcb-xfixes0-dev libxcb-composite0-dev libxcb-xinput-dev
libxfixes-dev libxcomposite-dev libxinerama-dev libxss-dev
libdrm-dev libva-dev libxcb-cursor-dev
libfontconfig1-dev libfreetype-dev
libpci-dev libpulse-dev libudev-dev libasound2-dev
```

Consequence: minors below 31.0 are probed in the container and can lower the
declared minimum, provided the container build installs with
`--component obs_libraries` rather than `--component Development`. That
component name is confirmed for 30.0.0 only — any other pre-31.x tag must be
checked against its own `cmake/Modules/ObsHelpers.cmake` before assuming
`obs_libraries` still applies.

## The supported OBS range is derived, not declared (2026-09-02)

The compat job tested a hard-coded version pair while the README claimed a
range by hand and OBS_VERSION named a third number. Nothing kept them in
agreement, and they had already drifted two minor versions apart.

The range now comes from evidence: one probe per OBS minor from 30.0 up,
compiled and linked against that version's SDK. Three choices are worth
recording, because each rules out a plausible alternative:

- **Compile and link, not runtime.** Honest about what CI can check, and the
  README says so in those words rather than implying more.
- **Contiguity.** The minimum is the start of the green block reaching the
  newest probed minor, not the lowest minor that compiles: a hole below the
  claimed floor would make the README false for exactly the people in it.
- **Polling, not events.** Actions cannot subscribe to another repository's
  releases, so a daily watch compares the newest OBS tag to the manifest and
  wakes the matrix only when OBS has moved.

First derived range: 30.0 – 32.2.2. Unverifiable in CI: none.

Every probed minor from 30.0.0 through 32.2.0, plus the newest stable tag
32.2.2, compiled and linked cleanly on the first real run (CI run
[33596311564](https://github.com/angeloruggieridj/obs-playlist-deck/actions/runs/33596311564)):
30.0.0/30.1.0/30.2.0 built in the jammy container (FFmpeg 4.4, below
`LEGACY_BOUNDARY`), 31.0.0 through 32.2.2 built natively on ubuntu-24.04. No
gaps, no obs-build failures, no plugin-build failures — the full grid was
green, so `min_supported` lands at the floor (30.0, where
`obs_frontend_add_dock_by_id()` was introduced) rather than somewhere above
it. The old hand-written README claimed "31.0+"; the measured floor is
actually one minor lower, and `OBS_VERSION` moved from the hand-set 32.1.2
to the CI-measured 32.2.2.

This first real run also surfaced a genuine defect the fixture-only tests
never could: `compat-discover` declared `needs: tests`, and `tests` always
fails its own `obs_compat.py --check` step when no manifest exists yet.
GitHub Actions skips every job downstream of a failed dependency regardless
of that job's own `if:` condition, so on a true first run the entire compat
matrix was skipped and no manifest could ever be produced — a bootstrap
deadlock, confirmed by dispatching the workflow before the fix (run
33595508885: `tests` failed, all nine other jobs skipped, zero artifacts).
Fixed by dropping `needs: tests` from `compat-discover` alone; `release`
still needs both `tests` and `compat-report`, so a failing test suite still
blocks a release.
