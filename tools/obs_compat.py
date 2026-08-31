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
