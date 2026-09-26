#!/usr/bin/env python3
"""Resolve the prebuilt dependencies, and the Qt, that a given OBS release uses.

The Windows and macOS jobs build OBS's SDK and then the plugin, and both have
to use the same dependencies OBS itself ships with. They used to name them by
hand: the Windows job compiled against Qt 6.7.3 while OBS 32.2 shipped 6.11.1,
and the macOS job pinned an obs-deps release a year older than the one OBS
32.2.2 declares. Nothing noticed, because the only guard compared the Qt the
build found against a second hand-written number.

Both answers are read from the source of truth instead: OBS's own
CMakePresets.json names the obs-deps release for a tag, and that obs-deps
release's build script names the Qt it contains. Moving OBS_VERSION moves both.

Usage: python3 tools/obs_deps.py --obs-version 32.2.2 --platform windows
Writes OBS_DEPS_VERSION, OBS_QT6_DEPS_VERSION and OBS_QT_ABI to $GITHUB_ENV,
or prints them when run outside Actions.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.request

PRESETS_URL = "https://raw.githubusercontent.com/obsproject/obs-studio/{tag}/CMakePresets.json"
QT_SCRIPT_URL = "https://raw.githubusercontent.com/obsproject/obs-deps/{tag}/deps.qt/{script}"
# obs-deps builds Qt with a different script per platform; each names the
# version it builds, and the two have agreed in every release checked, but the
# one for the platform being built is the one that counts.
QT_SCRIPTS = {"windows": "qt6.ps1", "macos": "qt6.zsh"}

_QT_VERSION = re.compile(r"""(?:\$Version\s*=\s*|local\s+(?:-r\s+)?version=)['"]?([0-9]+\.[0-9]+\.[0-9]+)""")


class ResolveError(Exception):
    """The dependency versions could not be determined."""


def deps_versions(presets: dict) -> dict[str, str]:
    """The obs-deps release of each dependency OBS pins, from CMakePresets.json."""
    for preset in presets.get("configurePresets", []):
        deps = (preset.get("vendor", {})
                      .get("obsproject.com/obs-studio", {})
                      .get("dependencies", {}))
        if deps:
            found = {name: deps[name].get("version") for name in ("prebuilt", "qt6")
                     if name in deps}
            if found.get("prebuilt") and found.get("qt6"):
                return found
    raise ResolveError("CMakePresets.json names no prebuilt/qt6 dependency versions")


def qt_version(script: str) -> str:
    match = _QT_VERSION.search(script)
    if not match:
        raise ResolveError("the obs-deps Qt build script names no Qt version")
    return match.group(1)


def major_minor(version: str) -> str:
    return ".".join(version.split(".")[:2])


def _fetch(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": "obs-playlist-deck-ci"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read().decode("utf-8")


def resolve(obs_version: str, platform: str) -> dict[str, str]:
    deps = deps_versions(json.loads(_fetch(PRESETS_URL.format(tag=obs_version))))
    script = _fetch(QT_SCRIPT_URL.format(tag=deps["qt6"], script=QT_SCRIPTS[platform]))
    return {
        "OBS_DEPS_VERSION": deps["prebuilt"],
        "OBS_QT6_DEPS_VERSION": deps["qt6"],
        "OBS_QT_ABI": major_minor(qt_version(script)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--obs-version", required=True)
    parser.add_argument("--platform", required=True, choices=sorted(QT_SCRIPTS))
    args = parser.parse_args()
    try:
        values = resolve(args.obs_version, args.platform)
    except (ResolveError, OSError, ValueError) as error:
        print(f"::error::cannot resolve the dependencies of OBS {args.obs_version}: {error}",
              file=sys.stderr)
        return 1

    lines = "".join(f"{name}={value}\n" for name, value in values.items())
    print(lines, end="")
    path = os.environ.get("GITHUB_ENV")
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(lines)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
