#!/usr/bin/env python3
"""Unit tests for tools/obs_compat.py.

The compat matrix decides what the README promises, so its logic is tested
offline against a fixture of the OBS tag list rather than against the live API.
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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

    def test_check_is_not_fooled_by_a_correct_block_sitting_outside_the_markers(self):
        # A correct copy anywhere in the file is not the same claim as the
        # marker region itself being correct; only the latter is what --write
        # actually produces, and only the latter is what --check must trust.
        with tempfile.TemporaryDirectory() as name:
            root = self._repo(Path(name))
            correct = obs_compat.render_readme_section(sample_manifest())
            readme = root / "README.md"
            readme.write_text(readme.read_text(encoding="utf-8") + "\n" + correct + "\n",
                              encoding="utf-8")
            problems = obs_compat.check(root)
            self.assertTrue(any("README" in problem for problem in problems))

    def test_write_raises_rather_than_leave_the_workflow_behind(self):
        # Single-quoted is valid YAML but not what _OBS_VERSION matches. If
        # write() stayed silent here it would move the README and leave
        # OBS_VERSION exactly where the whole command exists to stop it.
        with tempfile.TemporaryDirectory() as name:
            root = self._repo(Path(name))
            (root / ".github" / "workflows" / "build_project.yml").write_text(
                "env:\n  OBS_VERSION: '32.2.2'\n", encoding="utf-8")
            with self.assertRaises(obs_compat.RangeError):
                obs_compat.write(root)


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
        self.assertEqual(beta[0]["env"], "native")

    def test_bootstrap_requires_only_the_newest_stable(self):
        matrix = obs_compat.build_matrix(GRID, "32.2.2", None, None)
        required = [entry["obs"] for entry in matrix if entry["required"]]
        self.assertEqual(required, ["32.2.2"])


class NeedsFullRun(unittest.TestCase):
    def test_a_tag_push_always_runs_the_full_matrix(self):
        self.assertTrue(obs_compat.needs_full_run(
            "push", "", "refs/tags/v1.3.2", sample_manifest(), "32.2.2", None))

    def test_a_branch_push_with_an_unchanged_manifest_does_not_run_the_full_matrix(self):
        # This is what the watcher design rests on. If the push/tag check is
        # ever "simplified" to fire on any push rather than only a tag push,
        # this test is what catches it — otherwise every push to main would
        # start building OBS from source.
        self.assertFalse(obs_compat.needs_full_run(
            "push", "", "refs/heads/main", sample_manifest(), "32.2.2", None))

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


class _FakePage:
    """Stands in for the `urllib.request.urlopen` context manager's response.

    `link` is the raw `Link` header value fetch_tags() parses for pagination;
    an empty string reproduces a response with no Link header at all.
    """
    def __init__(self, tags: list[str], link: str = ""):
        self._body = json.dumps([{"name": tag} for tag in tags]).encode("utf-8")
        self._link = link

    def read(self) -> bytes:
        return self._body

    @property
    def headers(self) -> dict:
        return {"Link": self._link} if self._link else {}

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        return False


def _fake_urlopen(pages: list["_FakePage"]):
    """Serves `pages` in order, one per fetch_tags() request, ignoring the URL."""
    remaining = iter(pages)

    def _urlopen(request, timeout=30):
        return next(remaining)

    return _urlopen


def _next_link(url: str) -> str:
    return f'<{url}>; rel="next"'


class FetchTagsPagination(unittest.TestCase):
    def test_hitting_the_cap_raises_instead_of_truncating(self):
        # Four pages of 100 land exactly on TAG_CAP (400) while a next link is
        # still present — a silent truncation here would ship a short tag
        # list and, downstream, a wrong grid and a wrong README.
        pages = [
            _FakePage([f"30.{i}.0" for i in range(100)], _next_link("https://x/2")),
            _FakePage([f"31.{i}.0" for i in range(100)], _next_link("https://x/3")),
            _FakePage([f"32.{i}.0" for i in range(100)], _next_link("https://x/4")),
            _FakePage([f"33.{i}.0" for i in range(100)], _next_link("https://x/5")),
        ]
        with mock.patch.object(obs_compat.urllib.request, "urlopen", _fake_urlopen(pages)):
            with self.assertRaises(obs_compat.RangeError) as ctx:
                obs_compat.fetch_tags(None)
        self.assertIn("cap", str(ctx.exception))

    def test_a_response_under_the_cap_never_raises(self):
        pages = [_FakePage(["30.0.0", "30.1.0"])]
        with mock.patch.object(obs_compat.urllib.request, "urlopen", _fake_urlopen(pages)):
            tags = obs_compat.fetch_tags(None)
        self.assertEqual(tags, ["30.0.0", "30.1.0"])

    def test_a_missing_link_header_terminates_pagination_cleanly(self):
        pages = [_FakePage(["30.0.0", "30.1.0"], link="")]
        with mock.patch.object(obs_compat.urllib.request, "urlopen", _fake_urlopen(pages)):
            tags = obs_compat.fetch_tags(None)
        self.assertEqual(tags, ["30.0.0", "30.1.0"])

    def test_a_malformed_link_header_terminates_pagination_cleanly(self):
        # A Link header present but with no rel="next" part (e.g. only
        # rel="prev") must read the same as no Link header at all.
        pages = [_FakePage(["30.0.0"], link='<https://x/0>; rel="prev"')]
        with mock.patch.object(obs_compat.urllib.request, "urlopen", _fake_urlopen(pages)):
            tags = obs_compat.fetch_tags(None)
        self.assertEqual(tags, ["30.0.0"])


class Aggregate(unittest.TestCase):
    def test_it_reads_one_file_per_probe_from_nested_artifact_dirs(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for version, payload in (("30.0.0", unbuildable()), ("32.2.2", ok())):
                folder = root / f"compat-{version}"
                folder.mkdir()
                (folder / f"compat-{version}.json").write_text(
                    json.dumps({"obs": version, **payload}), encoding="utf-8")
            results, skipped = obs_compat.aggregate(root)
            self.assertEqual(results, {"30.0.0": unbuildable(), "32.2.2": ok()})
            self.assertEqual(skipped, [])

    def test_an_empty_artifact_dir_aggregates_to_nothing(self):
        with tempfile.TemporaryDirectory() as name:
            results, skipped = obs_compat.aggregate(Path(name))
            self.assertEqual(results, {})
            self.assertEqual(skipped, [])

    def test_an_empty_artifact_file_is_skipped_not_fatal(self):
        # The probe job that writes these files is explicitly allowed to
        # fail, so a truncated (here: zero-byte) artifact is realistic input.
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            broken = root / "compat-30.0.0"
            broken.mkdir()
            (broken / "compat-30.0.0.json").write_text("", encoding="utf-8")
            good = root / "compat-32.2.2"
            good.mkdir()
            (good / "compat-32.2.2.json").write_text(
                json.dumps({"obs": "32.2.2", **ok()}), encoding="utf-8")
            results, skipped = obs_compat.aggregate(root)
            self.assertEqual(results, {"32.2.2": ok()})
            self.assertEqual(len(skipped), 1)

    def test_a_malformed_artifact_file_is_skipped_not_fatal(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            broken = root / "compat-30.0.0"
            broken.mkdir()
            (broken / "compat-30.0.0.json").write_text("{not valid json", encoding="utf-8")
            good = root / "compat-32.2.2"
            good.mkdir()
            (good / "compat-32.2.2.json").write_text(
                json.dumps({"obs": "32.2.2", **ok()}), encoding="utf-8")
            results, skipped = obs_compat.aggregate(root)
            self.assertEqual(results, {"32.2.2": ok()})
            self.assertEqual(len(skipped), 1)

    def test_an_artifact_missing_the_obs_key_is_skipped_not_fatal(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            broken = root / "compat-30.0.0"
            broken.mkdir()
            (broken / "compat-30.0.0.json").write_text(json.dumps(ok()), encoding="utf-8")
            good = root / "compat-32.2.2"
            good.mkdir()
            (good / "compat-32.2.2.json").write_text(
                json.dumps({"obs": "32.2.2", **ok()}), encoding="utf-8")
            results, skipped = obs_compat.aggregate(root)
            self.assertEqual(results, {"32.2.2": ok()})
            self.assertEqual(len(skipped), 1)

    def test_a_version_with_an_unreadable_artifact_is_unverifiable_not_a_gap(self):
        # This is the semantic the fix exists to pin: an unreadable result
        # must read exactly like a probe that never reported, never like a
        # confirmed incompatibility.
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for version in GRID:
                folder = root / f"compat-{version}"
                folder.mkdir()
                if version == "30.0.0":
                    (folder / f"compat-{version}.json").write_text(
                        "{not valid json", encoding="utf-8")
                else:
                    (folder / f"compat-{version}.json").write_text(
                        json.dumps({"obs": version, **ok()}), encoding="utf-8")
            results, skipped = obs_compat.aggregate(root)
            self.assertEqual(len(skipped), 1)
            derived = obs_compat.derive_range(results, GRID, "32.2.2")
            self.assertEqual(derived["unverifiable"], ["30.0"])
            self.assertEqual(derived["gaps"], [])


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
                 obs_compat.EXIT_STALE, obs_compat.EXIT_NO_RANGE,
                 obs_compat.EXIT_BAD_INPUT}
        self.assertEqual(len(codes), 5)


class ReportMessageWhenDegraded(unittest.TestCase):
    """Verify the _report message accurately reflects when evidence is missing.

    Each test passes its own temp `root` straight into _report() (rather than
    patching the obs_compat.ROOT module attribute, which check() and
    save_manifest() never re-read because their path defaults are bound at
    function-definition time) so that _report reads and writes only the
    fixture README/workflow/manifest built here -- never the real repository.
    """

    def test_report_with_skipped_artifact_does_not_claim_all_green(self):
        # When an artifact is unreadable, do not claim every probe was green.
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            # Set up a mock repo
            (root / ".github" / "workflows").mkdir(parents=True)
            (root / "README.md").write_text(
                f"## Compatibility\n\n{obs_compat.README_START}\nstale\n"
                f"{obs_compat.README_END}\n\n## Building\n", encoding="utf-8")
            (root / ".github" / "workflows" / "build_project.yml").write_text(
                'env:\n  OBS_VERSION: "32.2.2"\n', encoding="utf-8")

            # Create artifact dir with all versions but 30.0.0 unreadable
            artifact_dir = root / "compat-artifacts"
            artifact_dir.mkdir()
            for version in GRID:
                folder = artifact_dir / f"compat-{version}"
                folder.mkdir()
                if version == "30.0.0":
                    # Create an unreadable artifact
                    (folder / f"compat-{version}.json").write_text("", encoding="utf-8")
                else:
                    (folder / f"compat-{version}.json").write_text(
                        json.dumps({"obs": version, **ok()}), encoding="utf-8")

            # Create an initial manifest so check() will find differences
            obs_compat.save_manifest(sample_manifest(), root / "obs-compat.json")

            # Set GITHUB_STEP_SUMMARY to a temp file (hermetic, real CI path, UTF-8)
            summary_file = root / "summary.txt"
            with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(summary_file)}):
                import io
                captured_stderr = io.StringIO()
                with mock.patch("sys.stderr", captured_stderr):
                    exit_code = obs_compat._report(artifact_dir, GRID, "32.2.2", None, root=root)

            self.assertEqual(exit_code, obs_compat.EXIT_STALE)
            stderr = captured_stderr.getvalue()
            # Should mention artifacts couldn't be read
            self.assertIn("artifact(s) could not be read", stderr)
            # Should NOT claim every probe was green
            self.assertNotIn("every probe is green", stderr)

            # Proof this consulted the fixture, not the real repo: the
            # initial manifest written above declared 30.0.0 ok and
            # unverifiable == []. Only a run against *this* artifact_dir and
            # *this* root produces 30.0.0 missing from results and "30.0"
            # promoted to unverifiable -- a run against the real repository
            # root could not have rewritten this file at all.
            written = obs_compat.load_manifest(root / "obs-compat.json")
            self.assertNotIn("30.0.0", written["results"])
            self.assertEqual(written["unverifiable"], ["30.0"])

    def test_report_with_obs_build_failure_does_not_claim_all_green(self):
        # When a probe failed at obs-build, do not claim every probe was green.
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            # Set up a mock repo
            (root / ".github" / "workflows").mkdir(parents=True)
            (root / "README.md").write_text(
                f"## Compatibility\n\n{obs_compat.README_START}\nstale\n"
                f"{obs_compat.README_END}\n\n## Building\n", encoding="utf-8")
            (root / ".github" / "workflows" / "build_project.yml").write_text(
                'env:\n  OBS_VERSION: "32.2.2"\n', encoding="utf-8")

            # Create artifact dir with all versions but 30.0.0 has obs-build failure
            artifact_dir = root / "compat-artifacts"
            artifact_dir.mkdir()
            for version in GRID:
                folder = artifact_dir / f"compat-{version}"
                folder.mkdir()
                if version == "30.0.0":
                    (folder / f"compat-{version}.json").write_text(
                        json.dumps({"obs": version, **unbuildable()}), encoding="utf-8")
                else:
                    (folder / f"compat-{version}.json").write_text(
                        json.dumps({"obs": version, **ok()}), encoding="utf-8")

            # Create an initial manifest so check() will find differences
            obs_compat.save_manifest(sample_manifest(), root / "obs-compat.json")

            # Set GITHUB_STEP_SUMMARY to a temp file (hermetic, real CI path, UTF-8)
            summary_file = root / "summary.txt"
            with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(summary_file)}):
                import io
                captured_stderr = io.StringIO()
                with mock.patch("sys.stderr", captured_stderr):
                    exit_code = obs_compat._report(artifact_dir, GRID, "32.2.2", None, root=root)

            self.assertEqual(exit_code, obs_compat.EXIT_STALE)
            stderr = captured_stderr.getvalue()
            # Should NOT claim every probe was green (because obs-build failures exist)
            self.assertNotIn("every probe is green", stderr)

            # Proof this consulted the fixture: 30.0.0's obs-build failure is
            # only visible in results/unverifiable if the manifest was
            # rebuilt from this test's own artifact_dir and written back to
            # this test's own root.
            written = obs_compat.load_manifest(root / "obs-compat.json")
            self.assertEqual(written["results"]["30.0.0"]["phase"], "obs-build")
            self.assertEqual(written["unverifiable"], ["30.0"])

    def test_report_fully_green_stale_run_claims_probes_are_green(self):
        # When all probes succeeded and nothing was skipped, the original message is used.
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            # Set up a mock repo
            (root / ".github" / "workflows").mkdir(parents=True)
            (root / "README.md").write_text(
                f"## Compatibility\n\n{obs_compat.README_START}\nstale\n"
                f"{obs_compat.README_END}\n\n## Building\n", encoding="utf-8")
            (root / ".github" / "workflows" / "build_project.yml").write_text(
                'env:\n  OBS_VERSION: "32.2.2"\n', encoding="utf-8")

            # Create artifact dir with all good artifacts
            artifact_dir = root / "compat-artifacts"
            artifact_dir.mkdir()
            for version in GRID:
                folder = artifact_dir / f"compat-{version}"
                folder.mkdir()
                (folder / f"compat-{version}.json").write_text(
                    json.dumps({"obs": version, **ok()}), encoding="utf-8")

            # Create an initial manifest so check() will find differences
            obs_compat.save_manifest(sample_manifest(), root / "obs-compat.json")

            # Set GITHUB_STEP_SUMMARY to a temp file (hermetic, real CI path, UTF-8)
            summary_file = root / "summary.txt"
            with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(summary_file)}):
                import io
                captured_stderr = io.StringIO()
                with mock.patch("sys.stderr", captured_stderr):
                    exit_code = obs_compat._report(artifact_dir, GRID, "32.2.2", None, root=root)

            self.assertEqual(exit_code, obs_compat.EXIT_STALE)
            stderr = captured_stderr.getvalue()
            # Should claim every probe is green
            self.assertIn("every probe is green", stderr)
            # Should NOT mention artifacts that couldn't be read
            self.assertNotIn("artifact(s) could not be read", stderr)

            # Proof this consulted the fixture: the summary file this test
            # pointed GITHUB_STEP_SUMMARY at is the only place the table
            # could have been written, since check(root) found the fixture
            # README stale (real content would never contain "stale").
            self.assertIn("32.2.2", summary_file.read_text(encoding="utf-8"))
            written = obs_compat.load_manifest(root / "obs-compat.json")
            self.assertEqual(written["unverifiable"], [])
            self.assertIn("30.0.0", written["results"])


class ReportRequiresItsArguments(unittest.TestCase):
    # Exit 1 is reserved for a genuine incompatibility; malformed CLI input
    # must never fall through argparse/json.loads into Python's default
    # exit-status-1 crash and be mistaken for one.
    def test_report_without_grid_is_bad_input_not_exit_1(self):
        argv = ["obs_compat.py", "--report", "--latest-stable", "32.2.2"]
        with mock.patch.object(sys, "argv", argv):
            self.assertEqual(obs_compat.main(), obs_compat.EXIT_BAD_INPUT)

    def test_report_without_latest_stable_is_bad_input(self):
        argv = ["obs_compat.py", "--report", "--grid", "[]"]
        with mock.patch.object(sys, "argv", argv):
            self.assertEqual(obs_compat.main(), obs_compat.EXIT_BAD_INPUT)

    def test_report_with_unparseable_grid_is_bad_input(self):
        argv = ["obs_compat.py", "--report", "--grid", "not-json",
                "--latest-stable", "32.2.2"]
        with mock.patch.object(sys, "argv", argv):
            self.assertEqual(obs_compat.main(), obs_compat.EXIT_BAD_INPUT)


if __name__ == "__main__":
    unittest.main()
