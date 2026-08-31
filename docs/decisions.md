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
