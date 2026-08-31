#!/usr/bin/env python3
"""Unit tests for tools/obs_compat.py.

The compat matrix decides what the README promises, so its logic is tested
offline against a fixture of the OBS tag list rather than against the live API.
"""
from __future__ import annotations

import json
import tempfile
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


class Manifest(unittest.TestCase):
    def test_absent_manifest_reads_as_none_for_bootstrap(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(obs_compat.load_manifest(Path(tmp) / "obs-compat.json"))

    def test_a_saved_manifest_round_trips(self):
        results = {version: ok() for version in GRID}
        results["30.0.0"] = unbuildable()
        built = obs_compat.build_manifest(results, GRID, "32.2.2", None, "2026-08-31")
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


if __name__ == "__main__":
    unittest.main()
