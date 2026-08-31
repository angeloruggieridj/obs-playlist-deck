#!/usr/bin/env python3
"""Verify that every place the version is written agrees.

A release packages the OBS plugin and the Stream Deck companion together, and
the two carried different numbers (plugin 1.2.6, companion 1.1.0) with nothing
to say which was current. The CHANGELOG is included as well, because a release
whose notes are for the previous version is worse than none.

Usage: python tools/check_version.py [expected-version]
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def cmake_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", text)
    if not match:
        raise SystemExit("error: no project(... VERSION ...) in CMakeLists.txt")
    return match.group(1)


def manifest_version() -> str:
    path = ROOT / "streamdeck" / "com.angeloruggieridj.playlist-deck.sdPlugin" / "manifest.json"
    return json.loads(path.read_text(encoding="utf-8"))["Version"]


def changelog_version() -> str:
    for line in (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").splitlines():
        match = re.match(r"^## \[([0-9]+\.[0-9]+\.[0-9]+)\]", line)
        if match:
            return match.group(1)
    raise SystemExit("error: no version heading in CHANGELOG.md")


def main() -> int:
    expected = sys.argv[1].lstrip("v") if len(sys.argv) > 1 else cmake_version()
    found = {
        "CMakeLists.txt": cmake_version(),
        "streamdeck manifest": manifest_version(),
        "CHANGELOG.md": changelog_version(),
    }
    problems = [f"{where}: {value} (expected {expected})"
                for where, value in found.items() if value != expected]
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"ok: everything reports {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
