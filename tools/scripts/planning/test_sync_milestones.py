"""Tests for keeping the milestones in step with the roadmap ledger."""

from __future__ import annotations

import datetime as dt
import importlib.util
import io
import json
from contextlib import redirect_stdout
from pathlib import Path
import sys
import tempfile
import unittest

_PATH = Path(__file__).resolve().parent / "sync_milestones.py"
_SPEC = importlib.util.spec_from_file_location("sync_milestones", _PATH)
assert _SPEC is not None and _SPEC.loader is not None
sm = importlib.util.module_from_spec(_SPEC)
sys.modules["sync_milestones"] = sm
_SPEC.loader.exec_module(sm)

D = dt.date.fromisoformat


def ledger(milestones: list[dict]) -> dict:
    return {
        "releases": [
            {"id": "mvp", "commitment": "2027-05-17"},
            {"id": "beta1", "commitment": "2027-10-04"},
            {"id": "beta", "commitment": "2028-05-08"},
        ],
        "milestones": milestones,
    }


BETA = {"number": 4, "title": "Stage 4 — Beta", "releases": ["beta1", "beta"], "description": "The Beta"}
MVP = {"number": 3, "title": "Stage 3 — MVP", "releases": ["mvp"], "description": "The MVP"}


class PlannedMilestonesTests(unittest.TestCase):
    def test_a_milestone_is_due_on_its_latest_release_commitment(self):
        (beta,) = sm.planned_milestones(ledger([BETA]))
        self.assertEqual(beta.due, D("2028-05-08"))

    def test_no_milestones_section_plans_nothing(self):
        self.assertEqual(sm.planned_milestones({"releases": []}), [])

    def test_an_unknown_release_is_refused(self):
        with self.assertRaisesRegex(sm.MilestoneError, "unknown release 'beta9'"):
            sm.planned_milestones(ledger([{**BETA, "releases": ["beta9"]}]))

    def test_a_release_in_two_milestones_is_refused(self):
        with self.assertRaisesRegex(sm.MilestoneError, "belongs to two milestones"):
            sm.planned_milestones(ledger([MVP, {**BETA, "releases": ["mvp"]}]))

    def test_a_repeated_number_is_refused(self):
        with self.assertRaisesRegex(sm.MilestoneError, "twice"):
            sm.planned_milestones(ledger([BETA, {**MVP, "number": 4}]))

    def test_a_milestone_with_no_release_is_refused(self):
        with self.assertRaisesRegex(sm.MilestoneError, "names no release"):
            sm.planned_milestones(ledger([{**BETA, "releases": []}]))


class ChangesTests(unittest.TestCase):
    def setUp(self):
        self.planned = sm.planned_milestones(ledger([MVP, BETA]))

    def current(self, **overrides) -> dict[int, dict]:
        out = {
            3: {"number": 3, "title": "Stage 3 — MVP", "description": "The MVP", "due_on": "2027-05-17T12:00:00Z"},
            4: {"number": 4, "title": "Stage 4 — Beta", "description": "The Beta", "due_on": "2028-05-08T07:00:00Z"},
        }
        for number, fields in overrides.items():
            out[int(number[1:])].update(fields)
        return out

    def test_nothing_changes_when_github_already_matches(self):
        # GitHub may store the due date at a different hour; only the date counts.
        self.assertEqual(sm.changes(self.planned, self.current()), [])

    def test_only_the_fields_that_differ_are_sent(self):
        current = self.current(m4={"title": "Stage 4 — Performance Beta", "due_on": "2027-08-15T00:00:00Z"})
        self.assertEqual(
            sm.changes(self.planned, current),
            [(4, {"title": "Stage 4 — Beta", "due_on": "2028-05-08T12:00:00Z"})],
        )

    def test_title_and_description_are_left_alone_when_the_ledger_gives_none(self):
        planned = sm.planned_milestones(ledger([{"number": 4, "releases": ["beta1", "beta"]}]))
        current = self.current(m4={"title": "Stage 4 — Performance Beta", "description": "old scope"})
        self.assertEqual(sm.changes(planned, current), [])

    def test_a_missing_description_is_filled_in(self):
        current = self.current(m3={"description": None})
        self.assertEqual(sm.changes(self.planned, current), [(3, {"description": "The MVP"})])

    def test_a_milestone_with_no_due_date_gets_one(self):
        current = self.current(m3={"due_on": None})
        self.assertEqual(sm.changes(self.planned, current), [(3, {"due_on": "2027-05-17T12:00:00Z"})])

    def test_a_milestone_missing_from_github_is_an_error_not_a_creation(self):
        current = self.current()
        del current[4]
        with self.assertRaisesRegex(sm.MilestoneError, "does not exist"):
            sm.changes(self.planned, current)

    def test_milestones_the_ledger_does_not_name_are_left_alone(self):
        current = self.current()
        current[1] = {"number": 1, "title": "Stage 1", "description": "", "due_on": "2026-08-30T00:00:00Z"}
        self.assertEqual(sm.changes(self.planned, current), [])


class DueOnTests(unittest.TestCase):
    def test_due_on_is_midday_utc(self):
        self.assertEqual(sm.due_on(D("2028-11-27")), "2028-11-27T12:00:00Z")


class RealLedgerTests(unittest.TestCase):
    def test_every_milestone_in_the_ledger_carries_a_commitment(self):
        raw = json.loads(sm.LEDGER_PATH.read_text(encoding="utf-8"))
        planned = {m.number: m for m in sm.planned_milestones(raw)}
        commitments = {r["id"]: r["commitment"] for r in raw["releases"]}
        self.assertEqual(planned[3].due.isoformat(), commitments["mvp"])
        self.assertEqual(planned[4].due.isoformat(), commitments["beta"])
        self.assertEqual(planned[5].due.isoformat(), commitments["v1"])

    def test_every_release_belongs_to_a_milestone(self):
        raw = json.loads(sm.LEDGER_PATH.read_text(encoding="utf-8"))
        named = {r for m in raw["milestones"] for r in m["releases"]}
        self.assertEqual(named, {r["id"] for r in raw["releases"]})


class OfflineRunTests(unittest.TestCase):
    def test_main_reports_without_editing_from_a_saved_listing(self):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
            json.dump([{"number": n, "title": "old", "description": "", "due_on": None} for n in (2, 3, 4, 5)], handle)
        out = io.StringIO()
        with redirect_stdout(out):
            code = sm.main(["--milestones-json", handle.name])
        Path(handle.name).unlink()
        self.assertEqual(code, 0)
        self.assertIn("would update milestone 5", out.getvalue())
        self.assertNotIn("updating", out.getvalue())


if __name__ == "__main__":
    unittest.main()
