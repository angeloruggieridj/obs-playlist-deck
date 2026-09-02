# OBS Compatibility Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CI derive the range of OBS Studio versions this plugin supports, record it in a versioned manifest, publish it in the README, and re-verify it whenever OBS ships a release.

**Architecture:** One stdlib-only Python script (`tools/obs_compat.py`) holds all the logic in four modes — `--discover` (query the OBS tag list, emit the Actions matrix), `--report` (aggregate probe results, derive the range, write the manifest), `--write` (render the README section and bump `OBS_VERSION`), `--check` (verify the three agree, offline). The workflow gains a cheap discovery job, two matrix jobs (native runner and `ubuntu:22.04` container) sharing a composite action, and a report job that gates the release.

**Tech Stack:** Python 3 (standard library only — no `requests`, no `packaging`), `unittest`, GitHub Actions, CMake/Ninja, the OBS Studio source tree.

**Spec:** [docs/superpowers/specs/2026-08-31-obs-compat-matrix-design.md](../specs/2026-08-31-obs-compat-matrix-design.md)

## Global Constraints

- **Python: standard library only.** `tools/check_version.py` and `tools/gen_locales.py` import nothing external and CI installs no Python packages. Use `urllib.request`, not `requests`. Write your own version comparison, do not reach for `packaging`.
- **House style for `tools/*.py`:** `#!/usr/bin/env python3`, a module docstring that explains *why* the file exists (not what it does), `from __future__ import annotations`, `ROOT = Path(__file__).resolve().parent.parent`, `def main() -> int`, `raise SystemExit(main())`. Read `tools/check_version.py` before writing a line.
- **Version floor: `30.0`.** Reason, verbatim for the manifest: `obs_frontend_add_dock_by_id() was introduced in OBS 30.0`. The grid never goes below it.
- **"Stable"** means a tag of the exact form `X.Y.Z` with no suffix. Anything with `-beta`, `-rc` or any other suffix is a prerelease.
- **Lower-bound candidates are `X.Y.0`**, the first patch of each minor — never the latest patch.
- **Upper bound is the highest stable overall, patch included** (`32.2.2` at the time of writing).
- **A beta enters the matrix only when strictly greater than the highest stable**, and never enters the declared range.
- **`obs-build` failure ≠ `plugin-build` failure.** The first is "unverifiable", the second is "incompatible". Never collapse them, in any layer.
- **No AI attribution in commits.** No `Co-Authored-By` trailer, no generator footer.
- **Commit message style:** `type(scope): lowercase summary`, then a blank line and a body explaining *why*. See `git log`.
- **Repository language is English** — code, comments, docs, commit messages.

**Spec correction adopted by this plan (Task 5):** the spec says Platforms and Qt rows "stay outside the markers". A Markdown table cannot be split by an HTML comment and still render as one table. The generator therefore owns the *whole* Compatibility table, with the Platforms and Qt rows as constants in the script. Same outcome, one table that renders.

---

### Task 1: Feasibility probe — does OBS 30.0.0 build in `ubuntu:22.04`?

The entire "the floor can finally move" premise rests on an unproven assumption: that jammy's FFmpeg 4.4 and Qt 6.2 can build OBS 30.0.0. Prove or disprove it before building anything on top. This task produces **an answer**, not machinery — the probe workflow is deleted at the end.

**Files:**
- Create (temporary, deleted in Step 6): `.github/workflows/probe-obs30.yml`
- Modify: `docs/decisions.md` (append the finding)

**Interfaces:**
- Consumes: nothing.
- Produces: a decision consumed by Task 6 — either `JAMMY_APT_PACKAGES` (the confirmed apt list for the legacy environment) or the finding that 30.x stays unverifiable.

- [ ] **Step 1: Write the probe workflow**

Create `.github/workflows/probe-obs30.yml`:

```yaml
# TEMPORARY. Answers one question: can OBS 30.0.0 be built in ubuntu:22.04?
# Delete once the answer is recorded in docs/decisions.md.
name: Probe OBS 30 in jammy
on: workflow_dispatch

permissions:
  contents: read

jobs:
  probe:
    runs-on: ubuntu-24.04
    container: ubuntu:22.04
    steps:
      - name: Install build deps
        run: |
          apt-get update
          DEBIAN_FRONTEND=noninteractive apt-get install -y \
            git cmake ninja-build pkg-config extra-cmake-modules g++ \
            qt6-base-dev qt6-base-private-dev qt6-svg-dev \
            libavcodec-dev libavformat-dev libavutil-dev libavdevice-dev \
            libavfilter-dev libswscale-dev libswresample-dev libx264-dev \
            libcurl4-openssl-dev libjansson-dev libmbedtls-dev \
            uthash-dev nlohmann-json3-dev zlib1g-dev libpng-dev \
            libpipewire-0.3-dev libwayland-dev libxkbcommon-dev libgl1-mesa-dev \
            libgles2-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev \
            libxcb-xinerama0-dev libxcb-randr0-dev libxcb-shm0-dev \
            libxcb-xfixes0-dev libxcb-composite0-dev libxcb-xinput-dev \
            libxfixes-dev libxcomposite-dev libxinerama-dev libxss-dev \
            libdrm-dev libva-dev libxcb-cursor-dev \
            libfontconfig1-dev libfreetype-dev \
            libpci-dev libpulse-dev libudev-dev libasound2-dev
      - name: Report toolchain versions
        run: |
          cmake --version | head -1
          g++ --version | head -1
          dpkg -s qt6-base-dev  | grep ^Version
          dpkg -s libavcodec-dev | grep ^Version
      - name: Clone OBS 30.0.0
        run: git clone --depth=1 --branch 30.0.0 https://github.com/obsproject/obs-studio.git /tmp/obs
      - name: Configure OBS
        run: |
          cmake -B /tmp/obs-build -S /tmp/obs -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF \
            -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF \
            -DENABLE_BROWSER=OFF -DENABLE_VLC=OFF
      - name: Build and install obs-frontend-api
        run: |
          cmake --build /tmp/obs-build --target obs-frontend-api --parallel
          cmake --install /tmp/obs-build --component Development --prefix /tmp/obs-dev
          find /tmp/obs-dev -name '*.h' | head -20
```

- [ ] **Step 2: Commit the probe on a branch and push**

```bash
git checkout -b probe/obs30-jammy
git add .github/workflows/probe-obs30.yml
git commit -m "chore(ci): temporary probe for building OBS 30.0.0 in jammy"
git push -u origin probe/obs30-jammy
```

- [ ] **Step 3: Run it and read the result**

Run: `gh workflow run probe-obs30.yml --ref probe/obs30-jammy` then `gh run watch`

Expected: one of two outcomes, both useful.
- **Success** — the `find` step prints `obs-frontend-api.h` and friends. Record the apt list above as confirmed.
- **Failure** — read *which* step failed. A missing/too-old package (`Qt6 6.x or higher is required`) is a real answer; a typo in the apt list is not. Fix obvious list errors and re-run, at most twice. Do not spend more than three runs here.

- [ ] **Step 4: If it failed, try the one fallback worth trying**

If the failure is Qt-related, re-run with the frontend disabled to check whether `libobs` alone builds:

```yaml
            -DENABLE_UI=OFF -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF \
```

If `obs-frontend-api` cannot be built in jammy, 30.x is **unverifiable** — this plugin needs `obs-frontend-api`, so an SDK without it proves nothing. Record that and move on. Do not try a third distro.

- [ ] **Step 5: Record the finding in `docs/decisions.md`**

Append a section. Write the outcome you actually observed, not the one you hoped for:

```markdown
## OBS 30.x is built in an ubuntu:22.04 container (2026-08-31)

`ubuntu-24.04` ships FFmpeg 7, which OBS 30-era code does not compile
against. That is an OBS build problem, not a plugin incompatibility, but the
compat matrix could not tell the two apart, so the declared minimum sat at
31.0 while the plugin's actual API floor is 30.0
(`obs_frontend_add_dock_by_id`).

Probing 30.0.0 in an `ubuntu:22.04` container (FFmpeg 4.4, Qt 6.2):
<OUTCOME — "obs-frontend-api builds and installs" or "fails: <reason>">.

Consequence: <"minors below 31.0 are probed in the container and can lower the
declared minimum" or "30.x stays reported as not verifiable in CI, never as
unsupported">.
```

- [ ] **Step 6: Delete the probe workflow and merge the finding**

```bash
git rm .github/workflows/probe-obs30.yml
git add docs/decisions.md
git commit -m "docs(ci): record whether OBS 30.x builds in a jammy container

The compat matrix cannot lower its floor below what it can build. Probing
30.0.0 in ubuntu:22.04 answers that once, so the rest of the work is built on
a measurement instead of an assumption."
git push
gh pr create --fill && gh pr merge --squash --delete-branch
```

---

### Task 2: Version parsing and grid selection

**Files:**
- Create: `tools/obs_compat.py`
- Create: `tools/fixtures/obs-tags.json`
- Test: `tools/test_obs_compat.py`

**Interfaces:**
- Consumes: nothing.
- Produces, for every later task:
  - `FLOOR: tuple[int, int]` = `(30, 0)`
  - `FLOOR_REASON: str`
  - `parse_version(tag: str) -> tuple[int, int, int, str] | None` — `(major, minor, patch, suffix)`, suffix `""` when stable; `None` when the tag is not a version
  - `is_stable(v: tuple[int, int, int, str]) -> bool`
  - `sort_key(v: tuple[int, int, int, str]) -> tuple` — orders stable above a prerelease of the same `X.Y.Z`
  - `select_grid(tags: list[str]) -> list[str]` — one `"X.Y.0"` per minor `>= FLOOR`, ascending
  - `highest_stable(tags: list[str]) -> str`
  - `qualifying_beta(tags: list[str]) -> str | None`

- [ ] **Step 1: Create the tag-list fixture**

Create `tools/fixtures/obs-tags.json` — the shape the GitHub tags API returns, trimmed to what the tests need. Note it deliberately includes a pre-30 tag, an out-of-order tag, and a non-version tag:

```json
[
  {"name": "32.2.2"},
  {"name": "32.2.0"},
  {"name": "32.2.0-rc2"},
  {"name": "32.2.0-beta3"},
  {"name": "32.1.2"},
  {"name": "32.1.0"},
  {"name": "32.0.4"},
  {"name": "32.0.0"},
  {"name": "31.1.2"},
  {"name": "31.1.0"},
  {"name": "31.0.4"},
  {"name": "31.0.0"},
  {"name": "30.2.3"},
  {"name": "30.2.0"},
  {"name": "30.1.2"},
  {"name": "30.1.0"},
  {"name": "30.0.2"},
  {"name": "30.0.0"},
  {"name": "29.1.3"},
  {"name": "29.0.0"},
  {"name": "obs-studio-27.0.0"}
]
```

- [ ] **Step 2: Write the failing tests**

Create `tools/test_obs_compat.py`:

```python
#!/usr/bin/env python3
"""Unit tests for tools/obs_compat.py.

The compat matrix decides what the README promises, so its logic is tested
offline against a fixture of the OBS tag list rather than against the live API.
"""
from __future__ import annotations

import json
import unittest
from pathlib import Path

import obs_compat

FIXTURE = Path(__file__).resolve().parent / "fixtures" / "obs-tags.json"


def fixture_tags() -> list[str]:
    return [entry["name"] for entry in json.loads(FIXTURE.read_text(encoding="utf-8"))]


class ParseVersion(unittest.TestCase):
    def test_stable_tag_has_an_empty_suffix(self):
        self.assertEqual(obs_compat.parse_version("32.2.2"), (32, 2, 2, ""))

    def test_prerelease_keeps_its_suffix(self):
        self.assertEqual(obs_compat.parse_version("32.2.0-beta3"), (32, 2, 0, "beta3"))

    def test_non_version_tags_are_rejected(self):
        self.assertIsNone(obs_compat.parse_version("obs-studio-27.0.0"))
        self.assertIsNone(obs_compat.parse_version("latest"))

    def test_only_a_bare_x_y_z_counts_as_stable(self):
        self.assertTrue(obs_compat.is_stable(obs_compat.parse_version("32.2.2")))
        self.assertFalse(obs_compat.is_stable(obs_compat.parse_version("32.2.0-rc2")))


class Ordering(unittest.TestCase):
    def test_a_stable_release_outranks_its_own_prereleases(self):
        key = obs_compat.sort_key
        self.assertGreater(key(obs_compat.parse_version("32.2.0")),
                           key(obs_compat.parse_version("32.2.0-rc2")))

    def test_rc_outranks_beta_of_the_same_version(self):
        key = obs_compat.sort_key
        self.assertGreater(key(obs_compat.parse_version("32.2.0-rc2")),
                           key(obs_compat.parse_version("32.2.0-beta3")))


class Grid(unittest.TestCase):
    def test_one_first_patch_per_minor_from_the_floor_up(self):
        self.assertEqual(
            obs_compat.select_grid(fixture_tags()),
            ["30.0.0", "30.1.0", "30.2.0", "31.0.0", "31.1.0",
             "32.0.0", "32.1.0", "32.2.0"],
        )

    def test_versions_below_the_floor_are_excluded(self):
        grid = obs_compat.select_grid(fixture_tags())
        self.assertNotIn("29.0.0", grid)
        self.assertNotIn("29.1.0", grid)

    def test_a_minor_with_no_x_y_0_tag_is_skipped(self):
        # A minor whose .0 was never tagged cannot back the claim "X.Y+".
        self.assertEqual(obs_compat.select_grid(["31.0.1", "31.1.0"]), ["31.1.0"])


class Bounds(unittest.TestCase):
    def test_highest_stable_ignores_prereleases(self):
        self.assertEqual(obs_compat.highest_stable(fixture_tags()), "32.2.2")

    def test_no_beta_when_the_newest_prerelease_predates_the_newest_stable(self):
        self.assertIsNone(obs_compat.qualifying_beta(fixture_tags()))

    def test_a_beta_ahead_of_stable_qualifies(self):
        tags = fixture_tags() + ["32.3.0-beta1"]
        self.assertEqual(obs_compat.qualifying_beta(tags), "32.3.0-beta1")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'obs_compat'`

- [ ] **Step 4: Write the minimal implementation**

Create `tools/obs_compat.py`:

```python
#!/usr/bin/env python3
"""Decide, record and publish which OBS Studio versions this plugin supports.

The compat job used to test a hard-coded pair of versions while the README
claimed a range by hand, so the two could disagree indefinitely — and did. Here
the range is derived from what CI actually compiled: one probe per OBS minor,
a contiguity rule, and a manifest the README is generated from.

An SDK that fails to build on our runner is recorded as unverifiable, never as
incompatible; conflating the two is what pinned the declared floor above the
plugin's real one.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# obs_frontend_add_dock_by_id(), which the dock is registered with, does not
# exist before this. There is nothing below it worth probing.
FLOOR = (30, 0)
FLOOR_REASON = "obs_frontend_add_dock_by_id() was introduced in OBS 30.0"

_TAG = re.compile(r"^([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z.]+))?$")


def parse_version(tag: str) -> tuple[int, int, int, str] | None:
    """Split a tag into (major, minor, patch, suffix); suffix is "" when stable."""
    match = _TAG.match(tag)
    if not match:
        return None
    major, minor, patch, suffix = match.groups()
    return int(major), int(minor), int(patch), suffix or ""


def is_stable(version: tuple[int, int, int, str]) -> bool:
    return version[3] == ""


def sort_key(version: tuple[int, int, int, str]) -> tuple:
    # A stable release outranks every prerelease of the same X.Y.Z, so the
    # stability flag sorts above the suffix text. Among prereleases the suffix
    # compares as text, which is why "rc2" > "beta3" — correct for OBS, and
    # the reason we never rely on it for anything but picking the newest.
    major, minor, patch, suffix = version
    return (major, minor, patch, 1 if suffix == "" else 0, suffix)


def _parsed(tags: list[str]) -> list[tuple[int, int, int, str]]:
    return [v for v in (parse_version(tag) for tag in tags) if v is not None]


def _format(version: tuple[int, int, int, str]) -> str:
    major, minor, patch, suffix = version
    base = f"{major}.{minor}.{patch}"
    return f"{base}-{suffix}" if suffix else base


def select_grid(tags: list[str]) -> list[str]:
    """One candidate per minor at or above the floor: its first patch, X.Y.0.

    The README claims "X.Y+", and the version that makes that sentence true is
    X.Y.0 — probing a later patch would verify a different claim. A minor whose
    .0 was never tagged is skipped rather than approximated.
    """
    minors = {
        (v[0], v[1])
        for v in _parsed(tags)
        if is_stable(v) and v[2] == 0 and (v[0], v[1]) >= FLOOR
    }
    return [f"{major}.{minor}.0" for major, minor in sorted(minors)]


def highest_stable(tags: list[str]) -> str:
    stable = [v for v in _parsed(tags) if is_stable(v)]
    if not stable:
        raise SystemExit("error: the tag list contains no stable OBS release")
    return _format(max(stable, key=sort_key))


def qualifying_beta(tags: list[str]) -> str | None:
    """The newest prerelease, but only when it is ahead of the newest stable.

    A prerelease of a line that has already shipped is not forward-looking
    information, it is history — which is exactly what the old hard-coded
    matrix was still testing.
    """
    pres = [v for v in _parsed(tags) if not is_stable(v)]
    if not pres:
        return None
    newest = max(pres, key=sort_key)
    stable = parse_version(highest_stable(tags))
    return _format(newest) if sort_key(newest) > sort_key(stable) else None
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: PASS, 12 tests.

- [ ] **Step 6: Commit**

```bash
git add tools/obs_compat.py tools/test_obs_compat.py tools/fixtures/obs-tags.json
git commit -m "feat(compat): parse OBS tags and pick one probe per minor

The grid takes X.Y.0 rather than the newest patch of each minor, because
'30.0+' is a claim about 30.0.0 specifically. A prerelease counts only when it
is ahead of the newest stable, so a beta of a line that already shipped stops
being treated as the future."
```

---

### Task 3: Derive the supported range

**Files:**
- Modify: `tools/obs_compat.py`
- Test: `tools/test_obs_compat.py`

**Interfaces:**
- Consumes: `parse_version`, `select_grid` from Task 2.
- Produces:
  - A probe result is `{"status": "ok" | "fail", "phase": None | "obs-build" | "plugin-build", "env": "native" | "jammy"}`
  - `derive_range(results: dict[str, dict], grid: list[str], max_tested: str) -> dict` returning `{"min_supported": str, "gaps": list[str], "unverifiable": list[str]}`, where `min_supported` is a minor like `"30.0"` and the two lists hold minors
  - `RangeError` — raised when no range can be derived

- [ ] **Step 1: Write the failing tests**

Append to `tools/test_obs_compat.py`, above the `__main__` block:

```python
def ok(env: str = "native") -> dict:
    return {"status": "ok", "phase": None, "env": env}


def incompatible(env: str = "native") -> dict:
    return {"status": "fail", "phase": "plugin-build", "env": env}


def unbuildable(env: str = "jammy") -> dict:
    return {"status": "fail", "phase": "obs-build", "env": env}


GRID = ["30.0.0", "30.1.0", "30.2.0", "31.0.0", "31.1.0", "32.0.0", "32.1.0", "32.2.0"]


class DeriveRange(unittest.TestCase):
    def test_all_green_declares_the_floor(self):
        results = {version: ok() for version in GRID}
        derived = obs_compat.derive_range(results, GRID, "32.2.2")
        self.assertEqual(derived["min_supported"], "30.0")
        self.assertEqual(derived["gaps"], [])
        self.assertEqual(derived["unverifiable"], [])

    def test_a_failing_middle_minor_raises_the_minimum(self):
        # 30.0 compiling does not make "30.0+" true when 30.1 does not.
        results = {version: ok() for version in GRID}
        results["30.1.0"] = incompatible()
        derived = obs_compat.derive_range(results, GRID, "32.2.2")
        self.assertEqual(derived["min_supported"], "30.2")
        self.assertEqual(derived["gaps"], ["30.1"])

    def test_an_unbuildable_sdk_is_unverifiable_not_incompatible(self):
        results = {version: ok() for version in GRID}
        results["30.0.0"] = unbuildable()
        results["30.1.0"] = unbuildable()
        derived = obs_compat.derive_range(results, GRID, "32.2.2")
        self.assertEqual(derived["min_supported"], "30.2")
        self.assertEqual(derived["unverifiable"], ["30.0", "30.1"])
        self.assertEqual(derived["gaps"], [])

    def test_a_missing_result_is_unverifiable_too(self):
        results = {version: ok() for version in GRID if version != "30.0.0"}
        derived = obs_compat.derive_range(results, GRID, "32.2.2")
        self.assertEqual(derived["min_supported"], "30.1")
        self.assertEqual(derived["unverifiable"], ["30.0"])

    def test_the_block_must_reach_the_top_of_the_grid(self):
        # The newest minor failing means we cannot say what the range is at all.
        results = {version: ok() for version in GRID}
        results["32.2.0"] = incompatible()
        with self.assertRaises(obs_compat.RangeError):
            obs_compat.derive_range(results, GRID, "32.2.2")

    def test_only_failures_below_the_minimum_are_reported(self):
        results = {version: ok() for version in GRID}
        results["30.0.0"] = incompatible()
        derived = obs_compat.derive_range(results, GRID, "32.2.2")
        self.assertEqual(derived["min_supported"], "30.1")
        self.assertEqual(derived["gaps"], ["30.0"])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: FAIL — `AttributeError: module 'obs_compat' has no attribute 'derive_range'`

- [ ] **Step 3: Write the minimal implementation**

Append to `tools/obs_compat.py`:

```python
class RangeError(Exception):
    """No supported range can be derived from these results."""


def _minor_label(candidate: str) -> str:
    major, minor, _patch, _suffix = parse_version(candidate)
    return f"{major}.{minor}"


def derive_range(results: dict[str, dict], grid: list[str], max_tested: str) -> dict:
    """The declared minimum is the start of the green block reaching the top.

    Not the lowest minor that compiles: if 30.0 passes and 30.1 does not,
    "30.0+" is a lie to everyone running 30.1. So the block is walked down from
    the newest minor and stops at the first candidate that is not green,
    whatever the reason.
    """
    if not grid:
        raise RangeError("the version grid is empty")

    newest = grid[-1]
    if results.get(newest, {}).get("status") != "ok":
        raise RangeError(
            f"the newest probed minor ({newest}) is not green, so no range "
            f"can be declared against {max_tested}"
        )

    index = len(grid) - 1
    while index > 0 and results.get(grid[index - 1], {}).get("status") == "ok":
        index -= 1

    gaps, unverifiable = [], []
    for candidate in grid[:index]:
        result = results.get(candidate)
        # A probe we never got back says as little as one whose SDK would not
        # build: both are unknown, and unknown is not the same as unsupported.
        if result is None or result.get("phase") == "obs-build":
            unverifiable.append(_minor_label(candidate))
        else:
            gaps.append(_minor_label(candidate))

    return {
        "min_supported": _minor_label(grid[index]),
        "gaps": gaps,
        "unverifiable": unverifiable,
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: PASS, 18 tests.

- [ ] **Step 5: Commit**

```bash
git add tools/obs_compat.py tools/test_obs_compat.py
git commit -m "feat(compat): derive the supported range by contiguity

The minimum is the start of the green block that reaches the newest probed
minor, not the lowest minor that happens to compile — a hole below it would
make the README false for everyone sitting in that hole. Probes whose SDK
could not be built, and probes that never reported, come back as unverifiable
rather than unsupported."
```

---

### Task 4: The manifest

**Files:**
- Modify: `tools/obs_compat.py`
- Test: `tools/test_obs_compat.py`

**Interfaces:**
- Consumes: `derive_range`, `FLOOR_REASON`.
- Produces:
  - `MANIFEST_PATH: Path` = `ROOT / "obs-compat.json"`
  - `load_manifest(path: Path = MANIFEST_PATH) -> dict | None` — `None` when absent (bootstrap)
  - `save_manifest(data: dict, path: Path = MANIFEST_PATH) -> None`
  - `build_manifest(results, grid, max_tested, beta, generated) -> dict`

- [ ] **Step 1: Write the failing tests**

Append to `tools/test_obs_compat.py`:

```python
import tempfile


class Manifest(unittest.TestCase):
    def test_absent_manifest_reads_as_none_for_bootstrap(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(obs_compat.load_manifest(Path(tmp) / "obs-compat.json"))

    def test_a_saved_manifest_round_trips(self):
        built = obs_compat.build_manifest(
            {version: ok() for version in GRID}, GRID, "32.2.2", None, "2026-08-31")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "obs-compat.json"
            obs_compat.save_manifest(built, path)
            self.assertEqual(obs_compat.load_manifest(path), built)

    def test_the_manifest_carries_the_range_and_the_floor_reason(self):
        built = obs_compat.build_manifest(
            {version: ok() for version in GRID}, GRID, "32.2.2", None, "2026-08-31")
        self.assertEqual(built["min_supported"], "30.0")
        self.assertEqual(built["max_tested"], "32.2.2")
        self.assertIsNone(built["beta_tested"])
        self.assertEqual(built["generated"], "2026-08-31")
        self.assertEqual(built["floor"]["version"], "30.0")
        self.assertIn("obs_frontend_add_dock_by_id", built["floor"]["reason"])

    def test_unverifiable_minors_are_kept_out_of_the_gaps(self):
        results = {version: ok() for version in GRID}
        results["30.0.0"] = unbuildable()
        built = obs_compat.build_manifest(results, GRID, "32.2.2", None, "2026-08-31")
        self.assertEqual(built["unverifiable"], ["30.0"])
        self.assertEqual(built["gaps"], [])
        self.assertEqual(built["results"]["30.0.0"]["phase"], "obs-build")

    def test_a_qualifying_beta_is_recorded_but_not_in_the_range(self):
        results = {version: ok() for version in GRID}
        results["32.3.0-beta1"] = ok()
        built = obs_compat.build_manifest(
            results, GRID, "32.2.2", "32.3.0-beta1", "2026-08-31")
        self.assertEqual(built["beta_tested"], "32.3.0-beta1")
        self.assertEqual(built["max_tested"], "32.2.2")
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: FAIL — `AttributeError: module 'obs_compat' has no attribute 'load_manifest'`

- [ ] **Step 3: Write the minimal implementation**

Add `import json` to the imports at the top of `tools/obs_compat.py`, then append:

```python
MANIFEST_PATH = ROOT / "obs-compat.json"


def load_manifest(path: Path = MANIFEST_PATH) -> dict | None:
    """The manifest, or None on the first ever run — see build_matrix's bootstrap."""
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def save_manifest(data: dict, path: Path = MANIFEST_PATH) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_manifest(results: dict[str, dict], grid: list[str], max_tested: str,
                   beta: str | None, generated: str) -> dict:
    derived = derive_range(results, grid, max_tested)
    return {
        "generated": generated,
        "floor": {"version": f"{FLOOR[0]}.{FLOOR[1]}", "reason": FLOOR_REASON},
        "min_supported": derived["min_supported"],
        "max_tested": max_tested,
        "beta_tested": beta,
        "gaps": derived["gaps"],
        "unverifiable": derived["unverifiable"],
        "results": dict(sorted(results.items())),
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: PASS, 23 tests.

- [ ] **Step 5: Commit**

```bash
git add tools/obs_compat.py tools/test_obs_compat.py
git commit -m "feat(compat): record the range and every probe in a manifest

obs-compat.json is what makes the range reviewable in a diff: the derived
bounds, the floor with the API that sets it, and the per-version status and
phase behind them. A missing file is the bootstrap case, not an error."
```

---

### Task 5: Render the README and check for drift

**Files:**
- Modify: `tools/obs_compat.py`
- Modify: `README.md:310-315`
- Test: `tools/test_obs_compat.py`

**Interfaces:**
- Consumes: `load_manifest`, `save_manifest`, `MANIFEST_PATH`.
- Produces:
  - `README_START: str` = `"<!-- obs-compat:start -->"`, `README_END: str` = `"<!-- obs-compat:end -->"`
  - `render_readme_section(manifest: dict) -> str` — the table, no markers
  - `replace_between_markers(text: str, body: str) -> str`
  - `workflow_obs_version(text: str) -> str` and `set_workflow_obs_version(text: str, version: str) -> str`
  - `check(root: Path = ROOT) -> list[str]` — the problems found, empty when clean
  - `write(root: Path = ROOT) -> None`

- [ ] **Step 1: Write the failing tests**

Append to `tools/test_obs_compat.py`:

```python
def sample_manifest(**overrides) -> dict:
    base = obs_compat.build_manifest(
        {version: ok() for version in GRID}, GRID, "32.2.2", None, "2026-08-31")
    base.update(overrides)
    return base


class ReadmeRendering(unittest.TestCase):
    def test_the_table_states_the_range_and_the_built_against_version(self):
        body = obs_compat.render_readme_section(sample_manifest())
        self.assertIn("**30.0 – 32.2.2**", body)
        self.assertIn("32.2.2", body)

    def test_the_table_says_the_check_is_compile_and_link(self):
        # Without this line "30.0+" promises more than the matrix verifies.
        body = obs_compat.render_readme_section(sample_manifest())
        self.assertIn("Compile and link", body)
        self.assertIn("not a runtime test", body)

    def test_unverifiable_minors_get_their_own_row(self):
        body = obs_compat.render_readme_section(
            sample_manifest(unverifiable=["30.0", "30.1"], min_supported="30.2"))
        self.assertIn("Not verifiable in CI", body)
        self.assertIn("30.0, 30.1", body)

    def test_no_unverifiable_row_when_there_is_nothing_to_report(self):
        self.assertNotIn("Not verifiable", obs_compat.render_readme_section(sample_manifest()))

    def test_a_beta_gets_its_own_row_and_stays_out_of_the_range(self):
        body = obs_compat.render_readme_section(sample_manifest(beta_tested="32.3.0-beta1"))
        self.assertIn("32.3.0-beta1", body)
        self.assertIn("**30.0 – 32.2.2**", body)

    def test_the_static_rows_survive_generation(self):
        body = obs_compat.render_readme_section(sample_manifest())
        self.assertIn("Platforms", body)
        self.assertIn("Qt", body)

    def test_replacing_between_markers_leaves_the_rest_alone(self):
        text = f"before\n{obs_compat.README_START}\nold\n{obs_compat.README_END}\nafter\n"
        out = obs_compat.replace_between_markers(text, "new")
        self.assertIn("before", out)
        self.assertIn("after", out)
        self.assertIn("new", out)
        self.assertNotIn("old", out)

    def test_missing_markers_are_an_error_not_a_silent_no_op(self):
        with self.assertRaises(obs_compat.RangeError):
            obs_compat.replace_between_markers("no markers here", "new")


class WorkflowVersion(unittest.TestCase):
    WORKFLOW = 'env:\n  OBS_VERSION: "32.1.2"\n  OBS_DEPS_VERSION: "2025-08-23"\n'

    def test_it_reads_the_env_value(self):
        self.assertEqual(obs_compat.workflow_obs_version(self.WORKFLOW), "32.1.2")

    def test_it_rewrites_only_obs_version(self):
        out = obs_compat.set_workflow_obs_version(self.WORKFLOW, "32.2.2")
        self.assertIn('OBS_VERSION: "32.2.2"', out)
        self.assertIn('OBS_DEPS_VERSION: "2025-08-23"', out)


class WriteThenCheck(unittest.TestCase):
    def _repo(self, tmp: Path, obs_version: str = "32.2.2") -> Path:
        (tmp / ".github" / "workflows").mkdir(parents=True)
        (tmp / "README.md").write_text(
            f"## Compatibility\n\n{obs_compat.README_START}\nstale\n"
            f"{obs_compat.README_END}\n\n## Building\n", encoding="utf-8")
        (tmp / ".github" / "workflows" / "build_project.yml").write_text(
            f'env:\n  OBS_VERSION: "{obs_version}"\n', encoding="utf-8")
        obs_compat.save_manifest(sample_manifest(), tmp / "obs-compat.json")
        return tmp

    def test_write_then_check_is_clean(self):
        with tempfile.TemporaryDirectory() as name:
            root = self._repo(Path(name))
            obs_compat.write(root)
            self.assertEqual(obs_compat.check(root), [])

    def test_check_flags_a_stale_readme(self):
        with tempfile.TemporaryDirectory() as name:
            root = self._repo(Path(name))
            problems = obs_compat.check(root)
            self.assertTrue(any("README" in problem for problem in problems))

    def test_check_flags_a_stale_obs_version(self):
        with tempfile.TemporaryDirectory() as name:
            root = self._repo(Path(name), obs_version="32.1.2")
            obs_compat.write(root)
            (root / ".github" / "workflows" / "build_project.yml").write_text(
                'env:\n  OBS_VERSION: "32.1.2"\n', encoding="utf-8")
            problems = obs_compat.check(root)
            self.assertTrue(any("OBS_VERSION" in problem for problem in problems))

    def test_check_reports_a_missing_manifest_rather_than_crashing(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "README.md").write_text("nothing", encoding="utf-8")
            self.assertTrue(any("obs-compat.json" in problem for problem in obs_compat.check(root)))
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: FAIL — `AttributeError: module 'obs_compat' has no attribute 'render_readme_section'`

- [ ] **Step 3: Write the minimal implementation**

Append to `tools/obs_compat.py`:

```python
README_START = "<!-- obs-compat:start -->"
README_END = "<!-- obs-compat:end -->"
WORKFLOW_REL = Path(".github") / "workflows" / "build_project.yml"

# Static rows. A Markdown table cannot be split by an HTML comment and still
# render, so the generator owns the whole table; edit these here.
STATIC_ROWS = [
    ("Platforms", "Windows x64, Linux x86_64, macOS universal (Intel + Apple Silicon)"),
    ("Qt", "Qt 6"),
]

_OBS_VERSION = re.compile(r'^(\s*OBS_VERSION:\s*")([^"]*)(")', re.MULTILINE)


def render_readme_section(manifest: dict) -> str:
    rows = [
        ("OBS Studio", f"**{manifest['min_supported']} – {manifest['max_tested']}**"),
        ("Verified by",
         "Compile and link against each version's OBS SDK in CI — not a runtime test."),
        ("Built against", manifest["max_tested"]),
    ]
    if manifest.get("beta_tested"):
        rows.append(("Also builds against", f"{manifest['beta_tested']} (prerelease, not supported)"))
    if manifest.get("unverifiable"):
        rows.append(("Not verifiable in CI", ", ".join(manifest["unverifiable"])))
    rows.extend(STATIC_ROWS)
    lines = ["| | |", "|---|---|"]
    lines += [f"| **{label}** | {value} |" for label, value in rows]
    return "\n".join(lines)


def replace_between_markers(text: str, body: str) -> str:
    start, end = text.find(README_START), text.find(README_END)
    if start == -1 or end == -1 or end < start:
        raise RangeError(f"README is missing the {README_START} / {README_END} markers")
    return text[:start] + README_START + "\n" + body + "\n" + text[end:]


def workflow_obs_version(text: str) -> str:
    match = _OBS_VERSION.search(text)
    if not match:
        raise RangeError("no OBS_VERSION in the workflow")
    return match.group(2)


def set_workflow_obs_version(text: str, version: str) -> str:
    return _OBS_VERSION.sub(lambda m: m.group(1) + version + m.group(3), text, count=1)


def write(root: Path = ROOT) -> None:
    """Render the README section and move OBS_VERSION to match the manifest.

    They move together because OBS_VERSION is the version the shipped binaries
    are compiled against: letting it lag behind max_tested is how "built and
    tested against X" quietly stops being true.
    """
    manifest = load_manifest(root / "obs-compat.json")
    if manifest is None:
        raise RangeError("no obs-compat.json to render from; run --report first")

    readme = root / "README.md"
    readme.write_text(
        replace_between_markers(readme.read_text(encoding="utf-8"),
                                render_readme_section(manifest)),
        encoding="utf-8")

    workflow = root / WORKFLOW_REL
    workflow.write_text(
        set_workflow_obs_version(workflow.read_text(encoding="utf-8"), manifest["max_tested"]),
        encoding="utf-8")


def check(root: Path = ROOT) -> list[str]:
    """Offline consistency: README, manifest and OBS_VERSION must agree."""
    manifest = load_manifest(root / "obs-compat.json")
    if manifest is None:
        return ["obs-compat.json is missing; run tools/obs_compat.py --report"]

    problems = []
    expected = render_readme_section(manifest)
    if expected not in (root / "README.md").read_text(encoding="utf-8"):
        problems.append(
            "README compatibility table does not match obs-compat.json; "
            "run: python3 tools/obs_compat.py --write")

    found = workflow_obs_version((root / WORKFLOW_REL).read_text(encoding="utf-8"))
    if found != manifest["max_tested"]:
        problems.append(
            f"workflow OBS_VERSION is {found} but the manifest was tested against "
            f"{manifest['max_tested']}; run: python3 tools/obs_compat.py --write")
    return problems
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: PASS, 37 tests.

- [ ] **Step 5: Put the markers into the real README**

Replace the Compatibility table body in `README.md` (currently lines 310-315) with the markers around the existing table, so `--write` has somewhere to render into:

```markdown
## Compatibility

<!-- obs-compat:start -->
| | |
|---|---|
| **OBS Studio** | **31.0+** (CI-certified). Built and tested against **32.1.2**. |
| **Platforms** | Windows x64, Linux x86_64, macOS universal (Intel + Apple Silicon) |
| **Qt** | Qt 6 |
<!-- obs-compat:end -->
```

Leave the stale numbers exactly as they are. They are replaced by real ones in Task 9, from evidence — writing them by hand now is the habit this whole plan exists to break.

- [ ] **Step 6: Commit**

```bash
git add tools/obs_compat.py tools/test_obs_compat.py README.md
git commit -m "feat(compat): generate the README table and keep OBS_VERSION with it

The Compatibility table is now rendered from the manifest between markers, and
--write moves the workflow's OBS_VERSION at the same time: that env is the
version the shipped binaries are built against, so letting it drift is how
'built and tested against X' stops being true without anyone noticing.

The generator owns the whole table rather than a fragment of it, because a
Markdown table split by an HTML comment does not render as a table."
```

---

### Task 6: `--discover` — the matrix and the watcher

**Files:**
- Modify: `tools/obs_compat.py`
- Test: `tools/test_obs_compat.py`

**Interfaces:**
- Consumes: `select_grid`, `highest_stable`, `qualifying_beta`, `load_manifest`.
- Produces:
  - `env_for(candidate: str) -> str` — `"jammy"` below OBS 31.0, else `"native"`
  - `build_matrix(grid, latest_stable, beta, manifest) -> list[dict]` — entries `{"obs": str, "env": str, "required": bool}`
  - `needs_full_run(event: str, schedule: str, ref: str, manifest, latest_stable, beta) -> bool`
  - `fetch_tags(token: str | None) -> list[str]` — the only function that touches the network
  - `WEEKLY_CRON: str` = `"0 7 * * 1"`

**If Task 1 found that OBS 30.x cannot be built in jammy:** set `LEGACY_BOUNDARY = (31, 0)` to `FLOOR` instead, so `env_for` always returns `"native"`, and the 30.x probes report `obs-build` failures — which the range logic already handles as unverifiable. Change nothing else.

- [ ] **Step 1: Write the failing tests**

Append to `tools/test_obs_compat.py`:

```python
class Environment(unittest.TestCase):
    def test_pre_31_probes_run_in_the_jammy_container(self):
        # ubuntu-24.04 ships FFmpeg 7, which OBS 30-era code will not build against.
        self.assertEqual(obs_compat.env_for("30.2.0"), "jammy")

    def test_31_and_later_run_on_the_native_runner(self):
        self.assertEqual(obs_compat.env_for("31.0.0"), "native")
        self.assertEqual(obs_compat.env_for("32.2.2"), "native")


class BuildMatrix(unittest.TestCase):
    def test_every_grid_candidate_plus_the_newest_stable_is_probed(self):
        matrix = obs_compat.build_matrix(GRID, "32.2.2", None, sample_manifest())
        self.assertEqual([entry["obs"] for entry in matrix], GRID + ["32.2.2"])

    def test_the_newest_stable_is_not_duplicated_when_it_is_already_x_y_0(self):
        matrix = obs_compat.build_matrix(GRID, "32.2.0", None, sample_manifest())
        self.assertEqual([entry["obs"] for entry in matrix].count("32.2.0"), 1)

    def test_exactly_the_two_extremes_are_required(self):
        matrix = obs_compat.build_matrix(GRID, "32.2.2", None, sample_manifest())
        required = [entry["obs"] for entry in matrix if entry["required"]]
        self.assertEqual(required, ["30.0.0", "32.2.2"])

    def test_the_declared_minimum_is_probed_at_its_first_patch(self):
        # The manifest stores a minor; the grid probes X.Y.0.
        matrix = obs_compat.build_matrix(
            GRID, "32.2.2", None, sample_manifest(min_supported="31.0"))
        required = [entry["obs"] for entry in matrix if entry["required"]]
        self.assertEqual(required, ["31.0.0", "32.2.2"])

    def test_a_qualifying_beta_is_probed_but_never_required(self):
        matrix = obs_compat.build_matrix(GRID, "32.2.2", "32.3.0-beta1", sample_manifest())
        beta = [entry for entry in matrix if entry["obs"] == "32.3.0-beta1"]
        self.assertEqual(len(beta), 1)
        self.assertFalse(beta[0]["required"])

    def test_bootstrap_requires_only_the_newest_stable(self):
        matrix = obs_compat.build_matrix(GRID, "32.2.2", None, None)
        required = [entry["obs"] for entry in matrix if entry["required"]]
        self.assertEqual(required, ["32.2.2"])


class NeedsFullRun(unittest.TestCase):
    def test_a_tag_push_always_runs_the_full_matrix(self):
        self.assertTrue(obs_compat.needs_full_run(
            "push", "", "refs/tags/v1.3.2", sample_manifest(), "32.2.2", None))

    def test_a_manual_dispatch_always_runs_the_full_matrix(self):
        self.assertTrue(obs_compat.needs_full_run(
            "workflow_dispatch", "", "refs/heads/main", sample_manifest(), "32.2.2", None))

    def test_the_weekly_cron_always_runs_the_full_matrix(self):
        self.assertTrue(obs_compat.needs_full_run(
            "schedule", obs_compat.WEEKLY_CRON, "refs/heads/main",
            sample_manifest(), "32.2.2", None))

    def test_the_daily_watch_stays_quiet_when_obs_has_not_moved(self):
        self.assertFalse(obs_compat.needs_full_run(
            "schedule", "0 6 * * *", "refs/heads/main", sample_manifest(), "32.2.2", None))

    def test_the_daily_watch_fires_when_obs_ships_a_stable(self):
        self.assertTrue(obs_compat.needs_full_run(
            "schedule", "0 6 * * *", "refs/heads/main", sample_manifest(), "32.3.0", None))

    def test_the_daily_watch_fires_when_a_beta_appears(self):
        self.assertTrue(obs_compat.needs_full_run(
            "schedule", "0 6 * * *", "refs/heads/main",
            sample_manifest(), "32.2.2", "32.3.0-beta1"))

    def test_bootstrap_runs_the_full_matrix(self):
        self.assertTrue(obs_compat.needs_full_run(
            "schedule", "0 6 * * *", "refs/heads/main", None, "32.2.2", None))
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: FAIL — `AttributeError: module 'obs_compat' has no attribute 'env_for'`

- [ ] **Step 3: Write the minimal implementation**

Add `import urllib.request` to the imports, then append to `tools/obs_compat.py`:

```python
# Below this, the runner's FFmpeg 7 cannot build OBS, so the probe moves into an
# ubuntu:22.04 container (FFmpeg 4.4). Raise this to FLOOR to disable the
# container path entirely — the older probes then report obs-build failures,
# which the range logic already treats as unverifiable rather than unsupported.
LEGACY_BOUNDARY = (31, 0)

WEEKLY_CRON = "0 7 * * 1"
TAGS_URL = "https://api.github.com/repos/obsproject/obs-studio/tags?per_page=100"


def env_for(candidate: str) -> str:
    version = parse_version(candidate)
    return "jammy" if (version[0], version[1]) < LEGACY_BOUNDARY else "native"


def build_matrix(grid: list[str], latest_stable: str, beta: str | None,
                 manifest: dict | None) -> list[dict]:
    """One entry per probe, with the two gate candidates marked required.

    On the first run there is no manifest and therefore no declared minimum to
    defend, so only the newest stable gates. A minor that is in the grid but not
    yet in the manifest is never required either: that is how a freshly
    published OBS gets measured before it gets promised.
    """
    candidates = list(grid)
    if latest_stable not in candidates:
        candidates.append(latest_stable)
    if beta:
        candidates.append(beta)

    required = {latest_stable}
    if manifest:
        required.add(f"{manifest['min_supported']}.0")

    return [{"obs": candidate,
             "env": env_for(candidate),
             "required": candidate in required}
            for candidate in candidates]


def needs_full_run(event: str, schedule: str, ref: str, manifest: dict | None,
                   latest_stable: str, beta: str | None) -> bool:
    """The daily watch only wakes the matrix when OBS has actually moved."""
    if event in ("workflow_dispatch", "push") and (event == "workflow_dispatch"
                                                   or ref.startswith("refs/tags/")):
        return True
    if event == "schedule" and schedule == WEEKLY_CRON:
        return True
    if manifest is None:
        return True
    return (latest_stable != manifest.get("max_tested")
            or beta != manifest.get("beta_tested"))


def fetch_tags(token: str | None) -> list[str]:
    """The only place this module touches the network."""
    request = urllib.request.Request(TAGS_URL, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "obs-playlist-deck-compat",
    })
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    tags: list[str] = []
    url: str | None = TAGS_URL
    while url and len(tags) < 400:
        request.full_url = url
        with urllib.request.urlopen(request, timeout=30) as response:
            tags += [entry["name"] for entry in json.loads(response.read().decode("utf-8"))]
            link = response.headers.get("Link", "")
        url = None
        for part in link.split(","):
            if 'rel="next"' in part:
                url = part[part.find("<") + 1:part.find(">")]
    return tags
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: PASS, 52 tests.

- [ ] **Step 5: Sanity-check the network path by hand**

Run: `python3 -c "import sys; sys.path.insert(0,'tools'); import obs_compat as c; t=c.fetch_tags(None); print(len(t), c.select_grid(t), c.highest_stable(t), c.qualifying_beta(t))"`

Expected: a tag count in the hundreds, the grid starting at `30.0.0`, a highest stable of `32.2.2` or newer, and a beta that is either `None` or strictly newer than that stable. If the API rate-limits you unauthenticated, retry with `GITHUB_TOKEN` exported — CI always has one.

- [ ] **Step 6: Commit**

```bash
git add tools/obs_compat.py tools/test_obs_compat.py
git commit -m "feat(compat): build the probe matrix and watch for OBS releases

Discovery resolves the world once and hands the same versions to every shard,
marks exactly two candidates as gates, and answers whether the heavy jobs need
to run at all: on a quiet day the daily watch ends in seconds, and on the day
OBS ships it starts the full matrix without anyone touching the repo."
```

---

### Task 7: `--report`, and the command-line entry point

**Files:**
- Modify: `tools/obs_compat.py`
- Test: `tools/test_obs_compat.py`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - `aggregate(artifact_dir: Path) -> dict[str, dict]` — reads `**/compat-*.json`
  - `summary_table(manifest: dict) -> str` — Markdown for the job step summary
  - `report(artifact_dir, latest_stable, beta, grid, root) -> int` — exit code
  - Exit codes: `0` clean, `1` incompatibility, `2` stale declaration, `3` no range derivable
  - CLI: `--discover`, `--report`, `--write`, `--check`

- [ ] **Step 1: Write the failing tests**

Append to `tools/test_obs_compat.py`:

```python
class Aggregate(unittest.TestCase):
    def test_it_reads_one_file_per_probe_from_nested_artifact_dirs(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for version, payload in (("30.0.0", unbuildable()), ("32.2.2", ok())):
                folder = root / f"compat-{version}"
                folder.mkdir()
                (folder / f"compat-{version}.json").write_text(
                    json.dumps({"obs": version, **payload}), encoding="utf-8")
            self.assertEqual(obs_compat.aggregate(root),
                             {"30.0.0": unbuildable(), "32.2.2": ok()})

    def test_an_empty_artifact_dir_aggregates_to_nothing(self):
        with tempfile.TemporaryDirectory() as name:
            self.assertEqual(obs_compat.aggregate(Path(name)), {})


class SummaryTable(unittest.TestCase):
    def test_it_names_the_phase_of_every_failure(self):
        manifest = sample_manifest()
        manifest["results"]["30.0.0"] = unbuildable()
        table = obs_compat.summary_table(manifest)
        self.assertIn("30.0.0", table)
        self.assertIn("obs-build", table)


class ExitCodes(unittest.TestCase):
    def test_the_codes_are_distinct(self):
        codes = {obs_compat.EXIT_OK, obs_compat.EXIT_INCOMPATIBLE,
                 obs_compat.EXIT_STALE, obs_compat.EXIT_NO_RANGE}
        self.assertEqual(len(codes), 4)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: FAIL — `AttributeError: module 'obs_compat' has no attribute 'aggregate'`

- [ ] **Step 3: Write the minimal implementation**

Add `import argparse`, `import datetime`, `import os`, `import sys` to the imports, then append:

```python
EXIT_OK = 0
EXIT_INCOMPATIBLE = 1
EXIT_STALE = 2
EXIT_NO_RANGE = 3


def aggregate(artifact_dir: Path) -> dict[str, dict]:
    results: dict[str, dict] = {}
    for path in sorted(artifact_dir.glob("**/compat-*.json")):
        payload = json.loads(path.read_text(encoding="utf-8"))
        version = payload.pop("obs")
        results[version] = payload
    return results


def summary_table(manifest: dict) -> str:
    lines = [
        f"### OBS compatibility — {manifest['min_supported']} – {manifest['max_tested']}",
        "",
        "| version | env | status | phase |",
        "|---|---|---|---|",
    ]
    for version, result in manifest["results"].items():
        mark = "✅" if result["status"] == "ok" else "❌"
        lines.append(f"| `{version}` | {result['env']} | {mark} {result['status']} "
                     f"| {result['phase'] or '—'} |")
    return "\n".join(lines) + "\n"


def _emit_output(name: str, value: str) -> None:
    path = os.environ.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(f"{name}<<__EOF__\n{value}\n__EOF__\n")
    else:
        print(f"{name}={value}")


def _discover() -> int:
    tags = fetch_tags(os.environ.get("GITHUB_TOKEN"))
    grid = select_grid(tags)
    latest = highest_stable(tags)
    beta = qualifying_beta(tags)
    manifest = load_manifest()
    full = needs_full_run(os.environ.get("GITHUB_EVENT_NAME", ""),
                          os.environ.get("GITHUB_SCHEDULE", ""),
                          os.environ.get("GITHUB_REF", ""),
                          manifest, latest, beta)
    matrix = build_matrix(grid, latest, beta, manifest)
    _emit_output("run_full", "true" if full else "false")
    _emit_output("grid", json.dumps(grid))
    _emit_output("latest_stable", latest)
    _emit_output("beta", beta or "")
    _emit_output("native", json.dumps([e for e in matrix if e["env"] == "native"]))
    _emit_output("jammy", json.dumps([e for e in matrix if e["env"] == "jammy"]))
    print(f"grid={grid} latest={latest} beta={beta} run_full={full}", file=sys.stderr)
    return EXIT_OK


def _report(artifact_dir: Path, grid: list[str], latest: str, beta: str | None) -> int:
    results = aggregate(artifact_dir)
    broken = [version for version, result in results.items()
              if result.get("phase") == "plugin-build"]
    try:
        manifest = build_manifest(results, grid, latest, beta,
                                  datetime.date.today().isoformat())
    except RangeError as error:
        print(f"::error::{error}", file=sys.stderr)
        return EXIT_NO_RANGE

    save_manifest(manifest)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(summary_table(manifest))
    else:
        print(summary_table(manifest))

    if broken:
        print(f"::error::the plugin does not build against {', '.join(sorted(broken))}. "
              f"This is an incompatibility, not a CI failure.", file=sys.stderr)
        return EXIT_INCOMPATIBLE

    problems = check()
    for problem in problems:
        print(f"::error::{problem}", file=sys.stderr)
    if problems:
        print("::notice::every probe is green — the declared range simply moved. "
              "Run: python3 tools/obs_compat.py --write", file=sys.stderr)
        return EXIT_STALE
    return EXIT_OK


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--discover", action="store_true")
    mode.add_argument("--report", action="store_true")
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    parser.add_argument("--artifacts", type=Path, default=ROOT / "compat-artifacts")
    parser.add_argument("--grid", help="JSON list, from the discover job")
    parser.add_argument("--latest-stable")
    parser.add_argument("--beta", default="")
    args = parser.parse_args()

    try:
        if args.discover:
            return _discover()
        if args.report:
            return _report(args.artifacts, json.loads(args.grid),
                           args.latest_stable, args.beta or None)
        if args.write:
            write()
            print("ok: README and OBS_VERSION now match obs-compat.json")
            return EXIT_OK
        problems = check()
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return EXIT_STALE if problems else EXIT_OK
    except RangeError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_NO_RANGE


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s tools -p "test_*.py" -v`
Expected: PASS, 56 tests.

- [ ] **Step 5: Verify the CLI refuses to guess**

Run: `python3 tools/obs_compat.py`
Expected: exit 2 from argparse, listing the four mutually exclusive modes.

Run: `python3 tools/obs_compat.py --check`
Expected: exit 2 with `error: obs-compat.json is missing; run tools/obs_compat.py --report` — the manifest does not exist yet, and saying so is the correct behaviour at this point.

- [ ] **Step 6: Commit**

```bash
git add tools/obs_compat.py tools/test_obs_compat.py
git commit -m "feat(compat): aggregate probe results and report with distinct codes

An incompatibility (exit 1) and a range that simply moved (exit 2) look alike
in a red workflow and are not alike at all: the first is a problem, the second
is a reminder to regenerate. Separate codes and separate messages are what keep
the daily watcher worth reading."
```

---

### Task 8: The composite probe action

**Files:**
- Create: `.github/actions/obs-compat-probe/action.yml`

**Interfaces:**
- Consumes: nothing from the Python module — the action is self-contained.
- Produces: an artifact `compat-<version>` containing `compat-<version>.json` with `{"obs", "status", "phase", "env"}`, which Task 7's `aggregate` reads.

- [ ] **Step 1: Write the action**

Create `.github/actions/obs-compat-probe/action.yml`:

```yaml
name: OBS compat probe
description: Build one OBS SDK and compile the plugin against it, recording the outcome.

inputs:
  obs-version:
    description: OBS tag to build against, e.g. 30.0.0 or 32.2.0-beta1
    required: true
  env-name:
    description: native or jammy — recorded in the result, and part of the cache key
    required: true

runs:
  using: composite
  steps:
    # Every failure below is caught and recorded rather than thrown, because
    # the phase is the whole point: an SDK that will not build says nothing
    # about the plugin, and must never be reported as an incompatibility.
    - name: Cache OBS dev
      id: cache-obsdev
      uses: actions/cache@caa296126883cff596d87d8935842f9db880ef25 # v5
      with:
        path: /tmp/obs-dev
        key: obsdev-linux-${{ inputs.obs-version }}-${{ inputs.env-name }}-v2

    - name: Build OBS dev
      id: obs
      if: steps.cache-obsdev.outputs.cache-hit != 'true'
      shell: bash
      run: |
        set -o pipefail
        status=ok
        {
          git clone --depth=1 --branch "${{ inputs.obs-version }}" \
            https://github.com/obsproject/obs-studio.git /tmp/obs
          # OBS 32.2's Linux helper calls add_dependencies(obs-studio <execs> <mods>),
          # which errors when both lists are empty — as they are here, since only
          # obs-frontend-api is built. Harmless on versions without the call.
          sed -i 's|add_dependencies(${target} ${obs_executables} ${obs_modules})|if(obs_executables OR obs_modules)\n      add_dependencies(${target} ${obs_executables} ${obs_modules})\n    endif()|' \
            /tmp/obs/cmake/linux/helpers.cmake || true
          cmake -B /tmp/obs-build -S /tmp/obs -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF \
            -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF \
            -DENABLE_BROWSER=OFF -DENABLE_VLC=OFF
          cmake --build /tmp/obs-build --target obs-frontend-api --parallel
          cmake --install /tmp/obs-build --component Development --prefix /tmp/obs-dev
        } 2>&1 | tail -200 || status=fail
        echo "status=$status" >> "$GITHUB_OUTPUT"

    - name: Build the plugin against it
      id: plugin
      if: steps.obs.outputs.status != 'fail'
      shell: bash
      run: |
        set -o pipefail
        status=ok
        {
          cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
            -DCMAKE_PREFIX_PATH="/tmp/obs-dev;/usr"
          cmake --build build
        } 2>&1 | tail -200 || status=fail
        echo "status=$status" >> "$GITHUB_OUTPUT"

    - name: Record the outcome
      if: always()
      shell: bash
      run: |
        if [ "${{ steps.obs.outputs.status }}" = "fail" ]; then
          status=fail; phase=obs-build
        elif [ "${{ steps.plugin.outputs.status }}" = "fail" ]; then
          status=fail; phase=plugin-build
        else
          status=ok; phase=null
        fi
        printf '{"obs":"%s","status":"%s","phase":%s,"env":"%s"}\n' \
          "${{ inputs.obs-version }}" "$status" \
          "$([ "$phase" = null ] && echo null || echo "\"$phase\"")" \
          "${{ inputs.env-name }}" > "compat-${{ inputs.obs-version }}.json"
        cat "compat-${{ inputs.obs-version }}.json"
        [ "$status" = ok ] || echo "::warning::probe ${{ inputs.obs-version }} failed at $phase"

    - name: Upload the result
      if: always()
      uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7
      with:
        name: compat-${{ inputs.obs-version }}
        path: compat-${{ inputs.obs-version }}.json

    # The result is recorded; whether it should fail the workflow is the
    # caller's decision, made with continue-on-error from the required flag.
    - name: Fail if the probe failed
      if: always()
      shell: bash
      run: |
        [ "${{ steps.obs.outputs.status }}" != "fail" ] || exit 1
        [ "${{ steps.plugin.outputs.status }}" != "fail" ] || exit 1
```

- [ ] **Step 2: Validate the YAML parses**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/actions/obs-compat-probe/action.yml')); print('ok')"`
Expected: `ok`. If PyYAML is not installed locally, skip this and rely on Task 9's dispatch run, where a malformed action fails immediately and loudly.

- [ ] **Step 3: Commit**

```bash
git add .github/actions/obs-compat-probe/action.yml
git commit -m "feat(ci): a shared probe that records why it failed

Both matrix jobs run the same steps against different base images, so the
steps live in one composite action. The probe never throws on a build failure:
it records the phase first, uploads it, and only then fails, because an SDK
that will not build must not reach the report as an incompatibility."
```

---

### Task 9: Wire the workflow

**Files:**
- Modify: `.github/workflows/build_project.yml` — `on.schedule` (lines 10-11), the `tests` job (lines 30-46), the `compat` job (lines 364-433), the `release` job's `needs` (lines 470-476)

**Interfaces:**
- Consumes: `tools/obs_compat.py` (Tasks 2-7), the composite action (Task 8).
- Produces: the running matrix, whose first real execution Task 10 uses.

- [ ] **Step 1: Add the daily watch cron**

Replace lines 10-11 of `.github/workflows/build_project.yml`:

```yaml
  schedule:
    # Daily watch: cheap. compat-discover compares the newest OBS release
    # against obs-compat.json and starts the heavy jobs only when OBS moved.
    - cron: "0 6 * * *"
    # Weekly full run: not all breakage is version-driven — runner images and
    # system dependencies rot on their own schedule.
    - cron: "0 7 * * 1"
```

- [ ] **Step 2: Add the offline check to the `tests` job**

After the `Versions agree` step (line 46), append:

```yaml
      # The README's compatibility table, obs-compat.json and OBS_VERSION are
      # three statements about the same thing; this is what stops them
      # disagreeing for months, and costs a second on every PR.
      - name: Compatibility declaration is in sync
        run: python3 tools/obs_compat.py --check
      - name: Compat matrix logic
        run: python3 -m unittest discover -s tools -p "test_*.py"
```

- [ ] **Step 3: Replace the `compat` job with the four new jobs**

Delete lines 364-433 (the comment block through the end of the old `compat` job) and put in their place:

```yaml
  # What OBS versions this plugin supports is derived here, not asserted by
  # hand: one probe per OBS minor, a contiguity rule, and a manifest the README
  # is generated from. See docs/superpowers/specs/2026-08-31-obs-compat-matrix-design.md
  compat-discover:
    name: OBS compat — discover
    needs: tests
    if: >-
      github.event_name == 'workflow_dispatch' ||
      github.event_name == 'schedule' ||
      startsWith(github.ref, 'refs/tags/')
    runs-on: ubuntu-24.04
    outputs:
      run_full: ${{ steps.discover.outputs.run_full }}
      native: ${{ steps.discover.outputs.native }}
      jammy: ${{ steps.discover.outputs.jammy }}
      grid: ${{ steps.discover.outputs.grid }}
      latest_stable: ${{ steps.discover.outputs.latest_stable }}
      beta: ${{ steps.discover.outputs.beta }}
    steps:
      - uses: actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6
      - id: discover
        env:
          GITHUB_TOKEN: ${{ github.token }}
          # Which cron fired: the weekly one always runs the full matrix.
          GITHUB_SCHEDULE: ${{ github.event.schedule }}
        run: python3 tools/obs_compat.py --discover

  compat:
    name: OBS compat ${{ matrix.obs }}
    needs: compat-discover
    if: needs.compat-discover.outputs.run_full == 'true'
    runs-on: ubuntu-24.04
    continue-on-error: ${{ !matrix.required }}
    strategy:
      fail-fast: false
      matrix:
        include: ${{ fromJSON(needs.compat-discover.outputs.native) }}
    steps:
      - uses: actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6
      - name: Install build deps
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build pkg-config extra-cmake-modules \
            qt6-base-dev qt6-base-private-dev qt6-svg-dev \
            libavcodec-dev libavformat-dev libavutil-dev libavdevice-dev \
            libavfilter-dev libswscale-dev libswresample-dev libx264-dev \
            libcurl4-openssl-dev libjansson-dev libmbedtls-dev libsimde-dev \
            uthash-dev nlohmann-json3-dev zlib1g-dev libpng-dev \
            libpipewire-0.3-dev libwayland-dev libxkbcommon-dev libgl1-mesa-dev \
            libgles2-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev \
            libxcb-xinerama0-dev libxcb-randr0-dev libxcb-shm0-dev \
            libxcb-xfixes0-dev libxcb-composite0-dev libxcb-xinput-dev \
            libxfixes-dev libxcomposite-dev libxinerama-dev libxss-dev \
            libdrm-dev libva-dev libxcb-cursor-dev \
            libqrcodegencpp-dev librist-dev libsrt-openssl-dev \
            libfontconfig1-dev libfreetype-dev \
            libpci-dev libpulse-dev libudev-dev libasound2-dev
      - uses: ./.github/actions/obs-compat-probe
        with:
          obs-version: ${{ matrix.obs }}
          env-name: native

  # ubuntu-24.04 ships FFmpeg 7, which OBS 30-era code does not build against.
  # That is an OBS build problem, not a plugin one, so the older SDKs are built
  # in jammy rather than written off as incompatible.
  compat-legacy:
    name: OBS compat ${{ matrix.obs }} (jammy)
    needs: compat-discover
    if: >-
      needs.compat-discover.outputs.run_full == 'true' &&
      needs.compat-discover.outputs.jammy != '[]'
    runs-on: ubuntu-24.04
    container: ubuntu:22.04
    continue-on-error: ${{ !matrix.required }}
    strategy:
      fail-fast: false
      matrix:
        include: ${{ fromJSON(needs.compat-discover.outputs.jammy) }}
    steps:
      - uses: actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6
      - name: Install build deps
        run: |
          apt-get update
          DEBIAN_FRONTEND=noninteractive apt-get install -y \
            git cmake ninja-build pkg-config extra-cmake-modules g++ \
            qt6-base-dev qt6-base-private-dev qt6-svg-dev \
            libavcodec-dev libavformat-dev libavutil-dev libavdevice-dev \
            libavfilter-dev libswscale-dev libswresample-dev libx264-dev \
            libcurl4-openssl-dev libjansson-dev libmbedtls-dev \
            uthash-dev nlohmann-json3-dev zlib1g-dev libpng-dev \
            libpipewire-0.3-dev libwayland-dev libxkbcommon-dev libgl1-mesa-dev \
            libgles2-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev \
            libxcb-xinerama0-dev libxcb-randr0-dev libxcb-shm0-dev \
            libxcb-xfixes0-dev libxcb-composite0-dev libxcb-xinput-dev \
            libxfixes-dev libxcomposite-dev libxinerama-dev libxss-dev \
            libdrm-dev libva-dev libxcb-cursor-dev \
            libfontconfig1-dev libfreetype-dev \
            libpci-dev libpulse-dev libudev-dev libasound2-dev
      - uses: ./.github/actions/obs-compat-probe
        with:
          obs-version: ${{ matrix.obs }}
          env-name: jammy

  compat-report:
    name: OBS compat — report
    needs: [compat-discover, compat, compat-legacy]
    if: always() && needs.compat-discover.outputs.run_full == 'true'
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6
      - uses: actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c # v8
        with:
          path: compat-artifacts
          pattern: compat-*
      - name: Derive the supported range
        run: |
          python3 tools/obs_compat.py --report \
            --artifacts compat-artifacts \
            --grid '${{ needs.compat-discover.outputs.grid }}' \
            --latest-stable '${{ needs.compat-discover.outputs.latest_stable }}' \
            --beta '${{ needs.compat-discover.outputs.beta }}'
      - name: Upload the manifest as it would be written
        if: always()
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7
        with:
          name: obs-compat-manifest
          path: obs-compat.json
```

- [ ] **Step 4: Make the release wait on the report**

In the `release` job's `needs` list (lines 470-476), add `compat-report`:

```yaml
    needs:
      - tests
      - linux
      - windows
      - macos
      - streamdeck
      - compat-report
```

- [ ] **Step 5: Verify the workflow parses and the graph is acyclic**

Run: `python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/build_project.yml')); print(sorted(d['jobs']))"`
Expected: the job list including `compat-discover`, `compat`, `compat-legacy`, `compat-report`.

Run: `gh workflow view build_project.yml` (after pushing the branch)
Expected: no "workflow file issues" banner.

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/build_project.yml
git commit -m "feat(ci): make the compat matrix decide what the README promises

The job was dispatch-only, continue-on-error and hard-coded to a version pair
that had gone stale, so it gated nothing and told nobody. Now discovery
resolves the grid once, two matrix jobs probe it on the runner and in jammy,
and the report derives the range — with only the declared minimum and the
newest stable able to fail the run.

A tag now waits on that report, so a release cannot ship a README claiming a
range CI will not stand behind. A daily watch re-runs the matrix on the day OBS
publishes, rather than on the day we happen to release."
```

---

### Task 10: Bootstrap the manifest from a real run

Everything so far has been tested against fixtures. This task produces the first manifest from evidence and, for the first time, states a supported range that is measured rather than remembered.

**Files:**
- Create: `obs-compat.json` (from the CI run, not by hand)
- Modify: `README.md`, `.github/workflows/build_project.yml` (`OBS_VERSION`), `docs/decisions.md`

- [ ] **Step 1: Push the branch and dispatch the full matrix**

```bash
git push -u origin feat/obs-compat-matrix
gh workflow run build_project.yml --ref feat/obs-compat-matrix
gh run watch
```

Expected: `compat-discover` reports `run_full=true` (no manifest exists, so bootstrap forces it), then eight or nine probes run. A cold cache means this takes a while; probes may fail, and that is data, not a setback.

- [ ] **Step 2: Read the report**

Open the `compat-report` job summary. Expected: a table with a row per probed version, each carrying a status and, on failure, a phase. Confirm that no `obs-build` failure has been reported as an incompatibility — if one has, the bug is in `derive_range` or the composite action's phase detection, not in the numbers.

- [ ] **Step 3: Bring the manifest down and regenerate**

```bash
gh run download --name obs-compat-manifest
python3 tools/obs_compat.py --write
git diff
```

Expected: `obs-compat.json` appears, the README's compatibility table now shows a measured range, and `OBS_VERSION` moves from `32.1.2` to the newest stable. Read that diff properly — it is the first time the repository states this range on evidence.

- [ ] **Step 4: Verify the check now passes**

Run: `python3 tools/obs_compat.py --check`
Expected: exit 0, no output on stderr.

Run: `python3 -m unittest discover -s tools -p "test_*.py"`
Expected: PASS.

- [ ] **Step 5: Record the outcome in `docs/decisions.md`**

Append, filling in what the run actually showed:

```markdown
## The supported OBS range is derived, not declared (2026-08-31)

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

First derived range: <MIN> – <MAX>. Unverifiable in CI: <LIST or "none">.
```

- [ ] **Step 6: Commit and open the pull request**

```bash
git add obs-compat.json README.md .github/workflows/build_project.yml docs/decisions.md
git commit -m "feat(compat): declare the OBS range CI measured

First manifest derived from a real matrix run, with the README table and
OBS_VERSION generated from it. The bump to OBS_VERSION changes the version the
shipped binaries are compiled against, which is why it lands here, where the
platform builds run against it before the number is believed."
gh pr create --fill
```

- [ ] **Step 7: Confirm the platform builds pass against the new `OBS_VERSION`**

Run: `gh pr checks --watch`
Expected: the Linux, Windows and macOS jobs are green against the new OBS version. If one fails, that failure is the point of doing this in a pull request — fix it here, or revert `OBS_VERSION` alone and record why the newest stable is not yet buildable.

---

## Self-Review

**Spec coverage.** Every section of the spec maps to a task: definition of "supported" and phases → Tasks 3 and 8; version grid → Task 2; range derivation and contiguity → Task 3; build environments → Tasks 1, 6, 8, 9; CI topology and triggers → Task 9; watching OBS releases → Tasks 6 and 9; failure messages and exit codes → Task 7; manifest → Task 4; README rendering and `OBS_VERSION` → Task 5; two levels of check → Tasks 5 and 9; testing → Tasks 2-7; the OBS 30/jammy risk → Task 1, with its fallback wired into Task 6's `LEGACY_BOUNDARY`.

**Deviation from the spec, adopted deliberately:** the generator owns the whole Compatibility table rather than a fragment of it, because a Markdown table split by an HTML comment does not render. Recorded at the top of this plan.

**Type consistency.** `parse_version` returns `(major, minor, patch, suffix)` everywhere. A probe result is `{"status", "phase", "env"}` in the composite action, in `aggregate`, in `derive_range` and in the manifest's `results`. `min_supported` is always a minor (`"30.0"`); grid candidates are always full versions (`"30.0.0"`); `build_matrix` is the single place that converts between them, via `f"{manifest['min_supported']}.0"`.
