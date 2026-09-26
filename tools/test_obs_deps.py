#!/usr/bin/env python3
"""Unit tests for tools/obs_deps.py, against the shapes the real files have."""
from __future__ import annotations

import unittest

import obs_deps

PRESETS = {
    "configurePresets": [
        {
            "name": "dependencies",
            "hidden": True,
            "vendor": {"obsproject.com/obs-studio": {"dependencies": {
                "prebuilt": {"version": "2026-07-15", "baseUrl": "https://example"},
                "qt6": {"version": "2026-07-15", "baseUrl": "https://example"},
                "cef": {"version": "6533", "revision": {"windows-x64": 2}},
            }}},
        },
        {"name": "macos", "inherits": ["dependencies"]},
    ]
}


class DepsVersions(unittest.TestCase):
    def test_reads_prebuilt_and_qt6_from_the_vendor_block(self):
        self.assertEqual(obs_deps.deps_versions(PRESETS),
                         {"prebuilt": "2026-07-15", "qt6": "2026-07-15"})

    def test_presets_without_a_dependency_block_are_an_error(self):
        with self.assertRaises(obs_deps.ResolveError):
            obs_deps.deps_versions({"configurePresets": [{"name": "macos"}]})

    def test_a_block_missing_qt6_is_an_error_not_a_partial_answer(self):
        presets = {"configurePresets": [{"vendor": {"obsproject.com/obs-studio": {
            "dependencies": {"prebuilt": {"version": "2026-07-15"}}}}}]}
        with self.assertRaises(obs_deps.ResolveError):
            obs_deps.deps_versions(presets)


class QtVersion(unittest.TestCase):
    def test_powershell_script(self):
        script = "param(\n    [string] $Name = 'qt6',\n    [string] $Version = '6.11.1',\n)"
        self.assertEqual(obs_deps.qt_version(script), "6.11.1")

    def test_zsh_script(self):
        self.assertEqual(obs_deps.qt_version("local name='qt6'\nlocal version=6.11.1\n"), "6.11.1")

    def test_a_script_naming_no_version_is_an_error(self):
        with self.assertRaises(obs_deps.ResolveError):
            obs_deps.qt_version("local name='qt6'\n")

    def test_the_abi_is_major_minor(self):
        self.assertEqual(obs_deps.major_minor("6.11.1"), "6.11")


if __name__ == "__main__":
    unittest.main()
