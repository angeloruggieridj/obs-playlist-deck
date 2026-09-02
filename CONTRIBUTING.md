# Contributing

Thanks for looking. This is a small project with a deliberate shape; a few
minutes here will save a round trip on review.

## Build and test

```bash
# unit tests: no OBS, no Qt, seconds to build
cmake -B build-tests -DBUILD_PLUGIN=OFF -DBUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

# the plugin itself (needs Qt 6 and the OBS development files)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Before pushing, the two checks CI also runs:

```bash
python tools/gen_locales.py --check   # locale files in sync with locales.json
python tools/check_version.py         # CMake, Stream Deck manifest, CHANGELOG agree
```

## Layout

| Where | What |
|---|---|
| `src/core/` | Plain C++17. No OBS, no Qt. Playlist model, playlist formats, path handling, the playback engine (end-of-clip modes, shuffle bag, staged clips), the playlist library, schedule rules, moved-file search, undo history, version compare. |
| `src/plugin/` | The OBS module: dock UI, the list's model and view, media source controller, settings store, worker threads, vendor API. |
| `tests/` | doctest. Unit tests for the core, plus source-inspection tests (see below). |
| `data/locale/` | Generated. Edit `tools/locales.json` instead. |
| `streamdeck/` | Buildless JS companion plugin. |

The playback engine drives a `pld::IMediaTransport`: the OBS controller at
runtime, a fake that records calls in the tests. If you are adding behaviour
that decides *what plays and when*, it belongs behind that interface.

**Put logic in `src/core/` when you can.** Anything that lives there can be
tested in seconds without OBS running, which is the difference between a decision
that is verified and one that is hoped for.

## The source-inspection tests

Some tests read the plugin sources as text and assert on what they find — that
no thread is detached, that the websocket callbacks do not touch the model
directly, that no user-visible string is hardcoded English. They exist because
the behaviour they protect needs a running OBS to exercise, and a regression
there is expensive and quiet.

They are a bridge, not the destination. If you move logic into the core and
cover it with a real unit test, delete the inspection test it replaces and say
so in the PR.

Each one carries a comment explaining the bug it prevents. If a change makes one
fail, read that comment first: either the change reintroduces the bug, or the
decision genuinely moved and the test should move with it — in the same commit,
with the comment updated.

## Working agreement

- **One finding, one PR, one CHANGELOG entry.** Small changes get reviewed.
- **Write the CHANGELOG entry for a reader, not a diff.** Say what was wrong,
  why it was wrong, and why the fix is right. The existing entries are the
  standard; it is the most useful documentation this project has.
- **Comment the why, not the what.** The interesting comments here explain a
  decision that looks arbitrary until you know what went wrong without it.
- **Every behaviour change adds or updates a test.**
- **Non-ASCII in a `QStringLiteral` must be a universal character name**
  (`↗`, not the raw character or a byte escape) — there is a test for this,
  and the bug it prevents shipped once already.
- Locale files are generated: change `tools/locales.json` and run the generator.
- Read [docs/decisions.md](docs/decisions.md) before undoing something that
  looks arbitrary — most of it was paid for with a bug.

## Updating the compatibility declaration

`obs-compat.json`, the README's Compatibility table and `build_project.yml`'s
`OBS_VERSION` are three statements about the same evidence, kept honest by
`tools/obs_compat.py --check` in CI. The evidence itself — which OBS SDKs the
plugin actually compiled against — only comes from the `compat-report` job in
`build_project.yml`, which runs on a tag push, a manual dispatch, or the
weekly schedule. Nothing in that job commits its result; it only uploads it.

When `compat-report` fails (or you want to pull in evidence it produced on a
green run), update the declaration locally:

1. Open the failed (or latest) `compat-report` run on GitHub Actions and
   download its `obs-compat-manifest` artifact.
2. Replace the repository's `obs-compat.json` with the downloaded file.
3. Run `python tools/obs_compat.py --write` and commit both files.

Running `--write` before step 1 is a no-op: it renders from whatever
`obs-compat.json` is already on disk, which is still the old, committed
manifest until step 1 replaces it — and `--check` was already passing
against that same old manifest, so nothing will appear to be wrong locally.

A `compat-report` run that fails because no range could be derived at all
(`EXIT_NO_RANGE` — the newest probed OBS minor is not green) never writes a
fresh manifest, so its `obs-compat-manifest` artifact is empty rather than a
stale copy of the committed file; see what the run actually reported before
following the steps above.

## Translations

Add or fix a language in `tools/locales.json`, run
`python tools/gen_locales.py`, and commit both the table and the regenerated
`data/locale/*.ini`. `--check` verifies every language has every key and keeps
the `%1` placeholders of the English text.

## Commit messages

Short imperative subject with a scope, the way the history reads today:
`fix(dock): stop staging the next clip on deactivate`. The CHANGELOG carries the
long explanation, so the commit body can stay brief.
