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

import json
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
    # A stable release outranks every prerelease of the same X.Y.Z. Among
    # prereleases, numeric runs within the suffix are compared as integers
    # (so beta9 < beta10 < rc1), while alphabetic runs compare as text
    # (rc > beta). This ensures double-digit prerelease numbers sort correctly.
    major, minor, patch, suffix = version
    if suffix == "":
        # Stable: rank above all prereleases
        return (major, minor, patch, 1)
    # Prerelease: split into alternating alpha and numeric runs, converting
    # numeric runs to ints for numerical comparison. Each run is wrapped in a
    # 2-tuple to ensure type safety: (0, int_value) for digits, (1, str_value)
    # for text. This prevents TypeError from comparing int to str directly.
    parts = re.findall(r"\d+|\D+", suffix)
    normalized = tuple(
        (0, int(part)) if part.isdigit() else (1, part)
        for part in parts
    )
    return (major, minor, patch, 0, normalized)


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
        if result is None:
            unverifiable.append(_minor_label(candidate))
        elif result.get("status") == "fail":
            # A failure: was it the plugin or the SDK?
            if result.get("phase") == "obs-build":
                unverifiable.append(_minor_label(candidate))
            else:
                gaps.append(_minor_label(candidate))
        # Else: status is ok, don't report success below the minimum

    return {
        "min_supported": _minor_label(grid[index]),
        "gaps": gaps,
        "unverifiable": unverifiable,
    }


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
