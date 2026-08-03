#!/usr/bin/env python3
"""Tests for the attended pass and for the review server's routes.

The attended pass runs against a fake driver and scripted input, so the
behavioural questions are exercised without an application or a person. The
server is started on an ephemeral loopback port and driven with `http.client` —
enough to prove the routes are wired to the decisions in `review.py`, which is
all the HTTP layer is meant to do.
"""

from __future__ import annotations

import http.client
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import attend as attend_module  # noqa: E402
import review  # noqa: E402
import server as server_module  # noqa: E402
import suites as suites_module  # noqa: E402
import surfaces  # noqa: E402
from driver import Reply  # noqa: E402
from store import Store  # noqa: E402

PROVENANCE = {
    "identity": "machine-one",
    "processor": "Test CPU",
    "cores": 8,
    "memory_bytes": 16 * 1024**3,
    "graphics": "Test GPU",
    "operating_system": "Linux",
    "display": "2560x1440",
    "attributes": {},
}

TUNER = next(s for s in surfaces.SURFACES if s.title == "The tuner, docked")
SHELL = next(s for s in surfaces.SURFACES if s.title == "The shell with no tool open")


class FakeDriver:
    def __init__(self, refuse: set[str] | None = None) -> None:
        self.pid = 1234
        self.refuse = refuse or set()
        self.opened: list[str] = []
        self.restarts = 0

    def restart(self) -> None:
        self.restarts += 1

    def open_state(self, state: str) -> Reply:
        self.opened.append(state)

        if state in self.refuse:
            return Reply(False, [], f"no approved state '{state}'")

        return Reply(True, [])


class SuiteTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.store = Store.open(self.root / "verification.db")
        self.addCleanup(self.store.close)
        self.run_id = self.store.start_run(
            provenance=PROVENANCE,
            commit="abc123",
            mode=surfaces.QUICK,
            resolutions=("default",),
        )

    def add_capture(self, surface, geometry: str = "default") -> int:
        image = self.root / f"{surface.state}-{geometry}.png"
        image.write_bytes(b"png")

        return self.store.record_capture(
            self.run_id,
            state=surface.state,
            title=surface.title,
            geometry=geometry,
            image_path=str(image),
            thumbnail_path=str(image),
            width=1280,
            height=800,
        )


class AnswerParsingTests(unittest.TestCase):
    def test_enter_alone_passes(self) -> None:
        self.assertEqual(attend_module.parse_answer(""), ("pass", "", ""))

    def test_a_failure_carries_its_reason(self) -> None:
        verdict, note, error = attend_module.parse_answer("f the needle never moves")

        self.assertEqual(verdict, "fail")
        self.assertEqual(note, "the needle never moves")
        self.assertEqual(error, "")

    def test_a_failure_without_a_reason_is_refused(self) -> None:
        """A failure with no detail costs a whole surface to learn anything from."""
        verdict, _, error = attend_module.parse_answer("f")

        self.assertEqual(verdict, "")
        self.assertTrue(error)

    def test_skip_is_available_for_something_not_examined(self) -> None:
        self.assertEqual(attend_module.parse_answer("s")[0], "skip")

    def test_quitting_is_recognised(self) -> None:
        self.assertEqual(attend_module.parse_answer("q")[2], "stop")

    def test_an_unknown_command_is_refused(self) -> None:
        self.assertTrue(attend_module.parse_answer("maybe")[2])


class AttendedPassTests(SuiteTestCase):
    def make_pass(self, answers: list[str], driver: FakeDriver | None = None):
        scripted = iter(answers)
        self.said: list[str] = []

        return attend_module.AttendedPass(
            store=self.store,
            run_id=self.run_id,
            driver=driver or FakeDriver(),
            ask=lambda _: next(scripted, "q"),
            say=self.said.append,
        )

    def test_only_behavioural_questions_are_asked(self) -> None:
        self.add_capture(TUNER)
        self.add_capture(SHELL)

        result = self.make_pass([""]).run()

        # The shell asks nothing behavioural; the tuner asks exactly one thing.
        self.assertEqual(result["answered"], 1)

    def test_an_answer_is_stored_against_the_run(self) -> None:
        capture_id = self.add_capture(TUNER)
        self.make_pass([""]).run()
        stored = {row["question"]: row for row in self.store.verdicts(capture_id)}

        self.assertEqual(stored["live-input"]["verdict"], "pass")
        self.assertTrue(stored["live-input"]["attended"])

    def test_a_failure_without_a_reason_is_asked_again(self) -> None:
        capture_id = self.add_capture(TUNER)
        self.make_pass(["f", "f the needle never moves"]).run()
        stored = {row["question"]: row for row in self.store.verdicts(capture_id)}

        self.assertEqual(stored["live-input"]["verdict"], "fail")
        self.assertEqual(stored["live-input"]["note"], "the needle never moves")

    def test_stopping_leaves_the_rest_outstanding(self) -> None:
        self.add_capture(TUNER)
        result = self.make_pass(["q"]).run()

        self.assertEqual(result["answered"], 0)
        self.assertTrue(review.outstanding(self.store, self.run_id, attended=True))

    def test_a_surface_that_cannot_be_reached_does_not_stop_the_pass(self) -> None:
        self.add_capture(TUNER)
        driver = FakeDriver(refuse={TUNER.state})
        result = self.make_pass([""], driver).run()

        self.assertEqual(result["answered"], 0)
        self.assertTrue(any("could not be reached" in line for line in self.said))

    def test_the_instruction_is_shown_before_the_question(self) -> None:
        """Its extras only mean anything once the tester has done the thing."""
        instructed = next(s for s in surfaces.SURFACES if s.instruction)
        self.add_capture(instructed)
        self.make_pass([""] * 4).run()

        self.assertTrue(any(instructed.instruction in line for line in self.said))

    def test_nothing_to_attend_is_not_an_error(self) -> None:
        self.add_capture(SHELL)
        result = self.make_pass([]).run()

        self.assertEqual(result, {"answered": 0, "remaining": 0})


class ServerTests(SuiteTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.capture_id = self.add_capture(TUNER)
        self.httpd = server_module.serve(self.store, self.run_id, port=0)
        self.addCleanup(self.httpd.server_close)
        self.port = self.httpd.server_address[1]
        # A short poll interval so tearing the server down between tests costs
        # milliseconds rather than half a second each time.
        thread = threading.Thread(target=self.httpd.serve_forever, args=(0.02,), daemon=True)
        thread.start()
        self.addCleanup(self.httpd.shutdown)

    def request(self, method: str, path: str, payload: dict | None = None):
        connection = http.client.HTTPConnection(server_module.HOST, self.port, timeout=5)
        body = json.dumps(payload) if payload is not None else None
        connection.request(
            method, path, body=body, headers={"Content-Type": "application/json"} if body else {}
        )
        response = connection.getresponse()
        raw = response.read()
        connection.close()

        try:
            return response.status, json.loads(raw)
        except ValueError:
            return response.status, raw

    def test_the_grid_is_served_from_the_store(self) -> None:
        status, view = self.request("GET", "/api/run")

        self.assertEqual(status, 200)
        self.assertEqual(view["groups"][0]["surface"], TUNER.title)

    def test_the_page_is_served(self) -> None:
        status, body = self.request("GET", "/")

        self.assertEqual(status, 200)
        self.assertIn(b"testing suite", body)

    def test_an_image_is_served(self) -> None:
        status, body = self.request("GET", f"/image?id={self.capture_id}")

        self.assertEqual(status, 200)
        self.assertEqual(body, b"png")

    def test_a_missing_image_is_reported_rather_than_served_blank(self) -> None:
        Path(self.store.capture(self.capture_id).image_path).unlink()
        status, payload = self.request("GET", f"/image?id={self.capture_id}")

        self.assertEqual(status, 404)
        self.assertEqual(payload["error"], "missing")

    def test_scoring_through_the_api(self) -> None:
        status, payload = self.request(
            "POST",
            "/api/score",
            {"capture_id": self.capture_id, "question": "looks-correct", "verdict": "pass"},
        )

        self.assertEqual(status, 200)
        self.assertEqual(payload["problems"], [])

    def test_a_failure_without_a_note_is_refused_by_the_api(self) -> None:
        status, payload = self.request(
            "POST",
            "/api/score",
            {"capture_id": self.capture_id, "question": "works", "verdict": "fail"},
        )

        self.assertEqual(status, 400)
        self.assertTrue(payload["problems"])

    def test_tagging_a_selection_through_the_api(self) -> None:
        second = self.add_capture(SHELL)
        status, payload = self.request(
            "POST", "/api/tag", {"capture_ids": [self.capture_id, second], "tag": "ugly"}
        )

        self.assertEqual(status, 200)
        self.assertEqual(payload["tagged"], 2)
        self.assertIn("ugly", self.store.tags_for(second))

    def test_removing_a_tag_through_the_api(self) -> None:
        self.store.apply_tag([self.capture_id], "ugly")
        self.request(
            "POST", "/api/tag", {"capture_ids": [self.capture_id], "tag": "ugly", "remove": True}
        )

        self.assertEqual(self.store.tags_for(self.capture_id), [])

    def test_adding_a_tag_to_the_vocabulary(self) -> None:
        self.request("POST", "/api/tags", {"name": "cramped"})
        _, tags = self.request("GET", "/api/tags")

        self.assertIn("cramped", {tag["name"] for tag in tags})

    def test_a_nameless_tag_is_refused(self) -> None:
        status, _ = self.request("POST", "/api/tags", {"name": "   "})

        self.assertEqual(status, 400)

    def test_commenting_through_the_api(self) -> None:
        status, _ = self.request(
            "POST", "/api/comment", {"capture_id": self.capture_id, "body": "unreadable"}
        )

        self.assertEqual(status, 200)
        self.assertEqual(len(self.store.comments_for(self.capture_id)), 1)

    def test_outstanding_questions_are_reported(self) -> None:
        _, pending = self.request("GET", "/api/outstanding")

        self.assertTrue(pending)

    def test_an_unknown_route_is_a_404(self) -> None:
        status, _ = self.request("GET", "/api/whatever")

        self.assertEqual(status, 404)

    def test_the_hub_lists_every_suite(self) -> None:
        _, view = self.request("GET", "/api/session")

        self.assertEqual(
            {entry["id"] for entry in view["suites"]},
            {suite.id for suite in suites_module.SUITES},
        )

    def test_the_hub_reports_what_needs_building(self) -> None:
        """Said up front, because a cold build is the slowest thing here."""
        _, view = self.request("GET", "/api/session")

        self.assertTrue(view["builds"])
        self.assertTrue(all("present" in entry for entry in view["builds"]))

    def test_an_unknown_suite_is_refused(self) -> None:
        status, payload = self.request("POST", "/api/run-suites", {"suites": ["invented"]})

        self.assertEqual(status, 400)
        self.assertIn("invented", payload["error"])

    def test_running_nothing_is_refused(self) -> None:
        status, _ = self.request("POST", "/api/run-suites", {"suites": []})

        self.assertEqual(status, 400)

    def test_a_run_can_be_selected(self) -> None:
        status, view = self.request("POST", "/api/select", {"run_id": self.run_id})

        self.assertEqual(status, 200)
        self.assertEqual(view["run_id"], self.run_id)

    def test_the_job_status_is_readable_before_anything_runs(self) -> None:
        status, job = self.request("GET", "/api/job")

        self.assertEqual(status, 200)
        self.assertEqual(job["state"], "idle")
        self.assertFalse(job["running"])

    def test_the_server_stays_on_loopback(self) -> None:
        """The store holds screenshots of unreleased software on a workstation."""
        self.assertEqual(self.httpd.server_address[0], "127.0.0.1")


if __name__ == "__main__":
    unittest.main()
