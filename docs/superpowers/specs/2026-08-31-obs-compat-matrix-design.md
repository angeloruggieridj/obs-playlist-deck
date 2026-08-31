# OBS compatibility matrix — Design

**Date:** 2026-08-31
**Status:** Approved

## Summary

Turn the dormant `compat` job into the mechanism that decides, and then
publishes, the range of OBS Studio versions this plugin supports. A discovery
step reads the OBS release list, a matrix compiles the plugin against one SDK
per OBS minor, and a report step derives a supported range, records it in a
versioned manifest, and renders it into the README. The same machinery runs
when OBS itself ships a new version, so the claim in the README is re-verified
against the world rather than against memory.

## Motivation

The current job never answers the question it was written for:

- It runs only on `workflow_dispatch` and a weekly `schedule`, is
  `continue-on-error: true`, and gates nothing.
- Its matrix is hard-coded to `["31.0.0", "32.2.0-beta2"]`. As of this writing
  OBS **32.2.2** is stable, so the job's "forward compatibility with the
  upcoming 32.2 line" tests a beta of a line that shipped months ago.
- The workflow's `OBS_VERSION` env is `32.1.2` and the README says
  "**31.0+** (CI-certified). Built and tested against **32.1.2**." Three
  numbers, three places, no mechanism keeping them in agreement.

The plugin's real floor is **OBS 30.0**: `plugin-main.cpp` calls
`obs_frontend_add_dock_by_id()`, introduced in 30.0. The declared minimum is
31.0 only because OBS 30.x does not build from source on `ubuntu-24.04`
(the runner's FFmpeg 7 against OBS 30-era code) — an OBS build problem, not a
plugin incompatibility. Nothing in the current setup distinguishes those two.

## Goals

- Derive the supported OBS range from evidence produced by CI, not by hand.
- Let the lower bound actually move down when a lower OBS version compiles.
- Re-check the range when OBS publishes a release, without a plugin release.
- Publish `min_supported` and `max_tested` in the README, kept honest by a
  check that runs on every PR.
- Fail a plugin release whose README claims a range CI cannot stand behind.

## Non-Goals (YAGNI)

- No runtime testing against OBS. The check is compile + link; the README says
  so in those words.
- No per-patch bisection. Granularity is the OBS minor.
- No bot that opens issues. A failing scheduled run already sends mail.
- No auto-commit of the README from CI. The manifest and README are edited by
  a human (or an agent) running `--write`, and verified by `--check`.

## What "supported" means

A candidate is **compatible** when the plugin configures, compiles and links
against that version's OBS SDK on Linux. This is a source and ABI-surface
check, not a behavioural one. The README states this explicitly, because a
bare "31.0+" implies more verification than the matrix performs.

Every probe therefore carries a **status** and a **phase**:

| status | phase | meaning |
|---|---|---|
| `ok` | — | OBS SDK built, plugin built against it |
| `fail` | `plugin-build` | genuine incompatibility |
| `fail` | `obs-build` | the SDK could not be built on our runner — **unverifiable**, not incompatible |

Collapsing `obs-build` into `fail` is what previously froze the floor at 31.0.
The two must stay distinct everywhere: artifact, manifest, report, README note.

## The version grid

`compat-discover` reads `https://api.github.com/repos/obsproject/obs-studio/tags`
(authenticated with `GITHUB_TOKEN`; the endpoint is paginated and the tag list
is long enough to need it) and derives:

**Lower-bound candidates** — one per OBS minor `>= 30.0`, and specifically
`X.Y.0`, the *first* patch of the minor. The README claims "30.0+"; the version
that makes that sentence true is literally `30.0.0`. Probing `30.0.2` would
verify a different claim. As of this writing the grid is `30.0.0, 30.1.0,
30.2.0, 31.0.0, 31.1.0, 32.0.0, 32.1.0, 32.2.0`.

**Upper bound** — the highest stable release overall, patch included
(`32.2.2` today), because that is what users have installed. "Stable" means a
tag of the exact form `X.Y.Z` with no suffix; anything carrying `-beta`, `-rc`
or any other suffix is a prerelease.

**Beta** — the highest prerelease, included only when strictly greater than the
highest stable. Today none qualifies: `32.2.0-beta3 < 32.2.2`. A beta never
enters the declared range; it is reported on its own line as forward-looking
information, because a moving target cannot be promised.

**Floor** — the grid never goes below 30.0. The manifest records the reason
(`obs_frontend_add_dock_by_id` since 30.0) so the bound reads as a fact about
the code rather than an arbitrary cut-off.

Discovery resolves these versions **once** and passes them to every shard as
job outputs, so all shards and the report agree on the same world even if OBS
publishes mid-run.

## Deriving the range

`min_supported` is the start of the **contiguous block** of green minors
reaching up to `max_tested` — not simply the lowest minor that compiles.

If 30.0 passed and 30.1 failed, "30.0+" would be false for everyone on 30.1.
In that case the declared minimum is the start of the upper block, and the gap
is recorded in the manifest with its status and phase.

`obs-build` failures are neither green nor a gap: they are holes of unknown
colour. A contiguous block may not start at an unverifiable minor, and any
unverifiable minor below the declared minimum is listed in the README note as
"not verifiable in CI", never as unsupported.

## Build environments

| candidate | environment | why |
|---|---|---|
| minor `< 31.0` | `container: ubuntu:22.04` | FFmpeg 4.4; OBS 30-era code does not build against the runner's FFmpeg 7 |
| minor `>= 31.0` | `ubuntu-24.04` runner | matches the release build jobs |

The mapping is explicit in the discovery script with that reasoning beside it,
not an implicit rule derived from version numbers at three call sites.

Implemented as **two jobs** — `compat` (native) and `compat-legacy`
(container) — sharing a composite action at
`.github/actions/obs-compat-probe/action.yml` (inputs: `obs-version`; produces
the result artifact). The single-job alternative, `container` set from a matrix
value with an empty string meaning "no container", relies on a subtle
expression behaviour; the duplication of one job block is the cheaper risk.

Cache key: `obsdev-linux-<version>-<native|jammy>-v2`. The `v2` bump is
mandatory — the existing `v1` keys hold native-only builds and must not be
served to container jobs.

## CI topology

```
compat-discover      cheap; resolves grid, outputs matrix + run_full
      |
      +--> compat          (native runner, minors >= 31.0)
      +--> compat-legacy   (ubuntu:22.04 container, minors < 31.0)
      |         each shard -> artifact compat-<version>.json
      |
      +--> compat-report   (if: always()) aggregate, derive, check
                  |
                  +--> release   (tags only)
```

- `strategy.fail-fast: false` on both matrix jobs.
- `continue-on-error` is driven by a per-candidate `required` flag, set by
  discovery on exactly two candidates: `<min_supported>.0` — the manifest
  stores a minor, the grid probes its first patch — and the discovered highest
  stable. Only those two can break the workflow. A minor that appears in the
  grid but not yet in the manifest is never `required`; that is how a newly
  published OBS minor gets measured before it is promised.

- **Bootstrap.** On the first run there is no manifest. `--discover` then marks
  only the highest stable as `required`, `--report` writes the manifest from
  whatever the matrix produced, and `--check` has something to compare against
  from the second run onward. No hand-written seed file.
- `compat-report` runs `if: always()`, aggregates the artifacts, writes the
  table to the job step summary, and fails when a `required` candidate is not
  green or when manifest/README/`OBS_VERSION` disagree with the evidence.
- `release` gains `needs: compat-report`. This is what makes "at every release"
  real: a tag whose README claims a range CI cannot stand behind is not
  published.

**Triggers.** The job's `if` gains a tag-ref condition alongside the existing
`workflow_dispatch` and `schedule`.

## Watching OBS releases

GitHub Actions cannot subscribe to another repository's release events, so the
workflow polls — but separates watching from building.

Two crons: `0 6 * * *` (daily, watch) and `0 7 * * 1` (weekly, full),
distinguished via `github.event.schedule`. `compat-discover` runs daily and
compares the discovered highest stable and qualifying beta against the
manifest's `max_tested` / `beta_tested`, emitting `run_full`:

```
run_full = tag push
        OR workflow_dispatch
        OR weekly cron
        OR discovered stable/beta differs from the manifest
```

On a quiet day the run ends in seconds and no heavy job starts. On the day OBS
ships, the full matrix starts by itself and answers "does the plugin still
build?" with no plugin release and nobody touching the repo. The weekly full
run stays, because not all breakage is version-driven — runner images and
system dependencies rot on their own schedule.

## Failure messages

The report must never conflate these two, or the daily watcher becomes noise
and gets ignored:

- **Incompatibility** — a `required` candidate failed at `plugin-build`.
  The message names the version and the phase, and points at the log.
- **Stale declaration** — every probe is green but the manifest, README or
  `OBS_VERSION` names an older version than the world. The message says the
  range moved and to run `python3 tools/obs_compat.py --write`, then commit.

Distinct exit codes and distinct wording. The second is a reminder; the first
is a problem.

## Manifest

`obs-compat.json` at the repository root — the versioned record of what CI
knew and when:

```json
{
  "generated": "2026-08-31",
  "floor": {
    "version": "30.0",
    "reason": "obs_frontend_add_dock_by_id() was introduced in OBS 30.0"
  },
  "min_supported": "30.0",
  "max_tested": "32.2.2",
  "beta_tested": null,
  "results": {
    "30.0.0": { "status": "ok", "phase": null, "env": "jammy" },
    "31.0.0": { "status": "ok", "phase": null, "env": "native" },
    "32.2.2": { "status": "ok", "phase": null, "env": "native" }
  }
}
```

`results` carries one entry per probed candidate; the excerpt above is
illustrative, not the full grid.

## README rendering

The Compatibility table lives between `<!-- obs-compat:start -->` and
`<!-- obs-compat:end -->` and is written by `--write` from the manifest. It
states the range, the version actually built against, any unverifiable minors,
and — in plain words — that the verification is a compile-and-link check
against the SDK, not a runtime test. Rows the generator does not own
(Platforms, Qt) stay outside the markers.

## Two levels of check

1. **Cheap, everywhere.** `tools/obs_compat.py --check` in the existing `tests`
   job: README section vs manifest vs the workflow's `OBS_VERSION`. No network,
   no build, runs on every PR and push, and catches the class of mistake that
   produced today's three-way disagreement.
2. **Real, on tag/schedule/dispatch.** `compat-report` compares the manifest
   against the evidence the matrix just produced.

Level 1 makes the repository internally consistent; level 2 makes it true.

`OBS_VERSION` is part of level 1 for a reason worth stating: it is the version
the *shipped* Linux, Windows and macOS builds are compiled against, so keeping
it equal to `max_tested` is what makes "built and tested against X" a fact
rather than a wish. `--write` moves it together with the README, in one
command, so the three never drift apart again — and because that bump changes
the real release builds, it lands in a pull request where those builds run
before the number is believed.

## `tools/obs_compat.py`

One script, four modes:

| mode | does |
|---|---|
| `--discover` | query the OBS tags API, apply the grid rules, emit the Actions matrix and `run_full` as job outputs |
| `--report` | aggregate `compat-*.json` artifacts, derive the range, write the manifest and the step summary |
| `--write` | render the README section from the manifest, and rewrite the workflow's `OBS_VERSION` to `max_tested` |
| `--check` | verify README against manifest against `OBS_VERSION`; non-zero with a diff on drift |

Network access is confined to `--discover`; every other mode is pure and
offline, which is what makes the logic testable.

## Testing

Written test-first. `tools/test_obs_compat.py`, run via `python3 -m unittest`
in the `tests` job, covers the pure logic against a checked-in fixture of the
OBS tag list:

- grid selection picks `X.Y.0` per minor, floors at 30.0, ignores tags below it
- highest stable ignores prereleases; `32.2.2` beats `32.2.0-beta3`
- a beta is included only when strictly greater than the highest stable
- contiguity: a failing middle minor moves the declared minimum up
- an `obs-build` failure never lowers the minimum and never reads as
  incompatible
- `run_full` is false only when stable and beta both match the manifest
- README rendering round-trips: `--write` then `--check` is clean
- `--check` fails, with a useful message, on each of the three disagreements

## Risks and fallbacks

**OBS 30.x may not build in `ubuntu:22.04` either.** Jammy ships Qt 6.2 and OBS
30 may want newer. This is unproven, and the whole "the floor can finally move"
premise rests on it. **The first task of the implementation plan is an isolated
feasibility probe of `30.0.0` in the container, before any of this machinery is
built on the assumption.** If it fails, nothing is wasted: the design already
carries the `obs-build` phase, 30.x stays declared as "not verifiable in CI",
and the minimum starts wherever verification succeeds.

**Cache growth.** Eight OBS Development-component installs. The install is the
headers and import libraries only, not the full build tree, so this is expected
to sit well inside GitHub's 10 GB per-repository cache budget; the report logs
the sizes so the assumption is observable rather than assumed.

**API rate limits.** `--discover` authenticates with `GITHUB_TOKEN`; the daily
watcher is one paginated request per run.

**A slower tag build.** With a cold cache a tag now waits on the matrix. In
practice the daily watcher keeps the cache warm, so the common case is a
plugin-only rebuild per shard.

## Files touched

| file | change |
|---|---|
| `tools/obs_compat.py` | new — discover / report / write / check |
| `tools/test_obs_compat.py` | new — unit tests, with tag-list fixture |
| `tools/fixtures/obs-tags.json` | new — checked-in API sample |
| `obs-compat.json` | new — the manifest |
| `.github/actions/obs-compat-probe/action.yml` | new — shared probe steps |
| `.github/workflows/build_project.yml` | `compat-discover`, `compat`, `compat-legacy`, `compat-report`; daily cron; `release` needs `compat-report`; `--check` in `tests` |
| `README.md` | Compatibility table between markers, with the compile-and-link wording |
| `docs/decisions.md` | why compile-and-link, why the contiguity rule, why polling |
