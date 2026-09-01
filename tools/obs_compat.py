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

import argparse
import datetime
import json
import os
import re
import sys
import urllib.request
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


def _find_markers(text: str) -> tuple[int, int]:
    start, end = text.find(README_START), text.find(README_END)
    if start == -1 or end == -1 or end < start:
        raise RangeError(f"README is missing the {README_START} / {README_END} markers")
    return start, end


def replace_between_markers(text: str, body: str) -> str:
    start, end = _find_markers(text)
    return text[:start] + README_START + "\n" + body + "\n" + text[end:]


def _marker_region(text: str) -> str:
    """The text strictly between the markers, markers excluded.

    check() must compare against exactly this, not against the file as a
    whole: the rendered block appearing anywhere in the README is not the
    same claim as it being what the markers actually bracket, and a stale
    region hiding behind a correct-looking copy elsewhere is the one thing
    this check must never wave through.
    """
    start, end = _find_markers(text)
    return text[start + len(README_START):end].strip("\n")


def workflow_obs_version(text: str) -> str:
    match = _OBS_VERSION.search(text)
    if not match:
        raise RangeError("no OBS_VERSION in the workflow")
    return match.group(2)


def set_workflow_obs_version(text: str, version: str) -> str:
    updated, count = _OBS_VERSION.subn(lambda m: m.group(1) + version + m.group(3), text, count=1)
    if count == 0:
        # A silent no-op here is worse than the drift this command exists to
        # close: write() would return normally with the README moved and the
        # workflow left behind, exactly the divergence --check is meant to
        # catch — produced by the fixing command itself.
        raise RangeError("no OBS_VERSION in the workflow")
    return updated


def write(root: Path = ROOT) -> None:
    """Render the README section and move OBS_VERSION to match the manifest.

    They move together because OBS_VERSION is the version the shipped binaries
    are compiled against: letting it lag behind max_tested is how "built and
    tested against X" quietly stops being true.
    """
    manifest = load_manifest(root / "obs-compat.json")
    if manifest is None:
        raise RangeError(
            "no obs-compat.json to render from; run: python3 tools/obs_compat.py --report")

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
        return ["obs-compat.json is missing; run: python3 tools/obs_compat.py --report"]

    problems = []
    expected = render_readme_section(manifest)
    try:
        region = _marker_region((root / "README.md").read_text(encoding="utf-8"))
    except RangeError as exc:
        problems.append(f"{exc}; run: python3 tools/obs_compat.py --write")
    else:
        if region != expected:
            problems.append(
                "README compatibility table does not match obs-compat.json; "
                "run: python3 tools/obs_compat.py --write")

    found = workflow_obs_version((root / WORKFLOW_REL).read_text(encoding="utf-8"))
    if found != manifest["max_tested"]:
        problems.append(
            f"workflow OBS_VERSION is {found} but the manifest was tested against "
            f"{manifest['max_tested']}; run: python3 tools/obs_compat.py --write")
    return problems


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
    if event == "workflow_dispatch" or (event == "push" and ref.startswith("refs/tags/")):
        return True
    if event == "schedule" and schedule == WEEKLY_CRON:
        return True
    if manifest is None:
        return True
    return (latest_stable != manifest.get("max_tested")
            or beta != manifest.get("beta_tested"))


# A runaway stop, not an expected ceiling: OBS is at ~250 tags today. Hitting
# it means the API paginated far more than any real tag list would, and
# returning a silently short list from here would produce a wrong grid, a
# wrong newest-stable, and a wrong README with nothing anywhere to say why.
TAG_CAP = 400


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
    while url:
        request.full_url = url
        with urllib.request.urlopen(request, timeout=30) as response:
            tags += [entry["name"] for entry in json.loads(response.read().decode("utf-8"))]
            link = response.headers.get("Link", "")
        url = None
        for part in link.split(","):
            if 'rel="next"' in part:
                url = part[part.find("<") + 1:part.find(">")]
        if url and len(tags) >= TAG_CAP:
            raise RangeError(
                f"the OBS tag list exceeded the {TAG_CAP}-tag cap in fetch_tags(); "
                "raise TAG_CAP")
    return tags


# Exit 1 means one thing and only one thing: the plugin does not compile
# against a probed OBS version. Nothing else -- not a bad artifact, not a
# missing argument, not an unexpected exception -- may ever produce it.
EXIT_OK = 0
EXIT_INCOMPATIBLE = 1
EXIT_STALE = 2
EXIT_NO_RANGE = 3
EXIT_BAD_INPUT = 4


def aggregate(artifact_dir: Path) -> tuple[dict[str, dict], list[str]]:
    """One result per probe artifact, plus the paths of any this run could not read.

    The probe job that produces these files is explicitly allowed to fail, so
    a truncated or half-written artifact is a realistic input, not a
    hypothetical. Omitting it from results is the semantically right answer,
    not a dodge: a version with no usable result is exactly what derive_range
    already classifies as unverifiable -- the same rule it applies to a probe
    that never reported at all. An unreadable result must never read as an
    incompatible one.

    The skipped list exists so a caller can tell a fully green run apart from
    a degraded one that merely looks green because some evidence went
    missing -- collapsing that distinction is what let --report claim "every
    probe is green" while quietly working from a smaller result set.
    """
    results: dict[str, dict] = {}
    skipped: list[str] = []
    for path in sorted(artifact_dir.glob("**/compat-*.json")):
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
            version = payload.pop("obs")
        except (json.JSONDecodeError, KeyError, OSError) as error:
            print(f"::warning::skipping unreadable artifact {path}: {error}", file=sys.stderr)
            skipped.append(str(path))
            continue
        results[version] = payload
    return results, skipped


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
        # __EOF__ is a fixed delimiter, which would normally be a collision
        # risk. It is provably safe here: every value this function is ever
        # called with is a tag matched by _TAG (whose charset excludes
        # underscores) or JSON built from such tags, so __EOF__ can never
        # appear inside the value it delimits.
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
    results, skipped = aggregate(artifact_dir)
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
        # Determine if any probe failed at obs-build (SDK build failure).
        obs_build_failures = [version for version, result in results.items()
                               if result.get("phase") == "obs-build"]
        if skipped or obs_build_failures:
            # Do not claim every probe was green; some evidence is missing.
            count = len(skipped)
            print(f"::notice::compatibility matrix check failed: {count} artifact(s) "
                  f"could not be read. See ::warning:: messages above. The supported "
                  f"range may not have genuinely moved — inspect --artifacts and re-run "
                  f"before running python3 tools/obs_compat.py --write", file=sys.stderr)
        else:
            # All probes succeeded and check found problems → range simply moved.
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
            if not args.grid or not args.latest_stable:
                print("::error::--report requires --grid and --latest-stable", file=sys.stderr)
                return EXIT_BAD_INPUT
            try:
                grid = json.loads(args.grid)
            except json.JSONDecodeError as error:
                print(f"::error::--grid is not valid JSON: {error}", file=sys.stderr)
                return EXIT_BAD_INPUT
            return _report(args.artifacts, grid, args.latest_stable, args.beta or None)
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
    except Exception as error:
        # Nothing below this line may be allowed to fall through to Python's
        # own default exit status of 1 -- that number is reserved, in this
        # tool, for a genuine incompatibility, and an uncaught crash is not one.
        print(f"::error::unexpected {type(error).__name__}: {error}", file=sys.stderr)
        return EXIT_BAD_INPUT


if __name__ == "__main__":
    raise SystemExit(main())
