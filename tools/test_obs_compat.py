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

    def test_double_digit_prerelease_numbers_sort_numerically(self):
        key = obs_compat.sort_key
        self.assertGreater(key(obs_compat.parse_version("32.2.0-beta10")),
                           key(obs_compat.parse_version("32.2.0-beta9")))

    def test_rc10_sorts_above_rc2(self):
        key = obs_compat.sort_key
        self.assertGreater(key(obs_compat.parse_version("32.2.0-rc10")),
                           key(obs_compat.parse_version("32.2.0-rc2")))

    def test_stable_still_outranks_high_prerelease_numbers(self):
        key = obs_compat.sort_key
        self.assertGreater(key(obs_compat.parse_version("32.2.0")),
                           key(obs_compat.parse_version("32.2.0-rc10")))


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


if __name__ == "__main__":
    unittest.main()
