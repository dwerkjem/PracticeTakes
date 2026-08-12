#!/usr/bin/env python3
"""The HTTP layer under the hub. Deliberately the thinnest part.

Every decision lives in `review.py` and `store.py`; this file routes, serialises,
and serves files. That is why there is no web framework here — the API is eight
endpoints and a static route, and a framework plus a bundler would be more
moving parts than the thing they serve.

Bound to loopback. The store holds screenshots of an application under
development on somebody's workstation; there is no reason for it to be reachable
from anywhere else, and a default that listens on every interface would be a
mistake nobody notices until it matters.

Standard library only.
"""

from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import time
from urllib.parse import parse_qs, urlparse

import freshness
import history as history_module
import launcher as launcher_module
import restart as restart_module
import review
import runner as runner_module
import suites as suites_module
import surfaces
from store import Store

WEB_ROOT = Path(__file__).resolve().parent / "web"
HOST = "127.0.0.1"

# Content types are chosen from this table rather than guessed, so nothing a
# request supplies can reach a response header.
CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".json": "application/json",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".md": "text/plain; charset=utf-8",
}


def web_assets(root: Path = WEB_ROOT) -> dict[str, Path]:
    """Every file the page may ask for, mapped from its request path.

    Built once by walking the directory, so serving a static file is a lookup
    in a fixed table rather than arithmetic on a path from the URL. A request
    either names something in here or gets a 404 -- there is no traversal to
    get wrong, and nothing outside this directory is reachable however the path
    is spelled.
    """
    resolved = root.resolve()

    return {
        str(path.relative_to(resolved).as_posix()): path
        for path in sorted(resolved.rglob("*"))
        if path.is_file() and path.suffix in CONTENT_TYPES
    }


WEB_FILES = web_assets()

# What the suite's code looked like when this process imported it. Read at
# import rather than passed in, so it is stamped at the one moment that is
# actually true -- see `freshness.suite_staleness_warning`.
SOURCES_LOADED_AT = freshness.newest_suite_source_time()

# Which process is answering. A restart replaces this process in place, so the
# pid is unchanged and the port is often back before anyone can observe it
# missing; a page waiting for the new hub has nothing else to key on, and
# reloading against the old one lands back on the fault it was escaping.
BOOT_ID = str(time.time_ns())


class ReviewSession:
    """What the hub is looking at, and the job that can give it something to look at.

    The run is not fixed at startup. A store with nothing in it is the normal way
    to arrive here -- it is what happens the first time anyone opens the hub -- so
    the session starts empty, the page offers to run something, and the session
    moves to whatever run that produces.
    """

    def __init__(self, store: Store, run_id: int | None = None) -> None:
        self.store = store
        self._run_id = run_id
        self.job = runner_module.Job(store=store)

        # One live application a reviewer can open on a surface, so a question
        # a screenshot raises can be answered by the thing itself.
        self.launch = launcher_module.ManualLaunch(runner_module.CONTROL_BUILD / "bin" / "PracticeTakes")

    @property
    def run_id(self) -> int | None:
        """The run being reviewed: the chosen one, the one just captured, or the newest."""
        if self.job.run_id is not None and self._run_id is None:
            return self.job.run_id

        if self._run_id is not None:
            return self._run_id

        newest = self.store.latest_run()

        return int(newest["id"]) if newest is not None else None

    def select(self, run_id: int | None) -> None:
        self._run_id = run_id

    def overview(self) -> dict:
        """Enough for the page to render even when there is nothing to review."""
        run_id = self.run_id

        return {
            "run_id": run_id,
            "runs": [
                {
                    "id": int(row["id"]),
                    "started_at": row["started_at"],
                    "mode": row["mode"],
                    "commit": row["commit_hash"],
                    "complete": bool(row["complete"]),
                }
                for row in self.store.runs()
            ],
            "suites": [
                {
                    "id": suite.id,
                    "label": suite.label,
                    "kind": suite.kind,
                    "description": suite.description,
                    "needs_display": suite.needs_display,
                    "needs": list(suite.needs),
                }
                for suite in suites_module.SUITES
            ],
            "kinds": list(suites_module.KINDS),
            "builds": runner_module.build_overview(),
            "display": runner_module.display_available(),
            "stale_server": freshness.suite_staleness_warning(SOURCES_LOADED_AT),
            "boot": BOOT_ID,
            "job": self.job.status(),
            "launch": self.launch.status(),
            "modes": [surfaces.QUICK, surfaces.FULL],
            "resolutions": list(surfaces.SWEEP_GEOMETRIES),
            "themes": list(surfaces.THEMES),
            "results": [
                {
                    "suite": row["suite"],
                    "cases": row["cases"],
                    "failures": row["failures"],
                    "duration_seconds": row["duration_seconds"],
                    "recorded_at": row["recorded_at"],
                }
                for row in (self.store.test_results(run_id) if run_id else [])
            ],
            "measurements": [
                {
                    "metric": row["metric"],
                    "value": row["value"],
                    "unit": row["unit"],
                    "scenario": row["scenario"],
                }
                for row in (self.store.measurements(run_id) if run_id else [])
            ],
        }


class ReviewHandler(BaseHTTPRequestHandler):
    """One review session's requests. The session is attached by `serve`."""

    session: ReviewSession

    @property
    def store(self) -> Store:
        return self.session.store

    @property
    def run_id(self) -> int | None:
        return self.session.run_id

    # Quiet by default: a page load is a dozen requests and the reviewer does
    # not need a log of them.
    def log_message(self, *_: object) -> None:  # noqa: A003 - BaseHTTPRequestHandler's name
        return

    # --- Plumbing -----------------------------------------------------------

    def _send_json(self, payload: object, status: int = 200) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path, status: int = 200) -> None:
        """Send a file this server chose — never one a request named directly."""
        try:
            body = path.read_bytes()
        except OSError:
            self._send_json({"error": f"{path.name} is not readable"}, status=404)

            return

        self.send_response(status)
        self.send_header(
            "Content-Type", CONTENT_TYPES.get(path.suffix, "application/octet-stream")
        )
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", "0") or 0)

        if not length:
            return {}

        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return {}

    def _image(self, query: dict[str, list[str]], *, thumbnail: bool) -> None:
        try:
            capture_id = int(query.get("id", ["0"])[0])
        except ValueError:
            self._send_json({"error": "id must be a number"}, status=400)

            return

        capture = self.store.capture(capture_id)

        if capture is None:
            self._send_json({"error": f"no capture {capture_id}"}, status=404)

            return

        unavailable = review.image_available(capture)

        if unavailable:
            # Never a blank tile: the reviewer has to be able to tell a failed
            # capture from a pruned one from one that simply looks fine.
            self._send_json({"error": unavailable}, status=404)

            return

        wanted = Path(capture.thumbnail_path if thumbnail else capture.image_path).resolve()
        images = (self.store.path.parent / "images").resolve()

        # A row in the database is not a licence to read any file on the disk.
        # Captures live under the store's own image directory; anything else is
        # a store that has been edited, and is refused rather than served.
        if images not in wanted.parents:
            self._send_json({"error": "that image is not in the store"}, status=404)

            return

        self._send_file(wanted)

    # --- Routes -------------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler's name
        # A review is a long session over a store another process may be
        # writing. One request that hits something unreadable should say so and
        # leave the rest of the page working, rather than printing a traceback
        # to the terminal and dropping the connection.
        try:
            self._get()
        except Exception as error:  # noqa: BLE001 - the last line before a dead request
            self._send_json({"error": f"{type(error).__name__}: {error}"}, status=500)

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler's name
        try:
            self._post()
        except Exception as error:  # noqa: BLE001 - the last line before a dead request
            self._send_json({"error": f"{type(error).__name__}: {error}"}, status=500)

    def _get(self) -> None:
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)

        if parsed.path in ("/", "/index.html"):
            self._send_file(WEB_FILES["index.html"])
        elif parsed.path == "/api/run":
            run_id = self.run_id

            if run_id is None:
                # Nothing captured yet. Not an error: it is the first thing that
                # happens to anyone, and the page has a button for it.
                self._send_json({"empty": True, **self.session.overview()})
            else:
                self._send_json({"empty": False, **review.run_view(self.store, run_id),
                                 **self.session.overview()})
        elif parsed.path == "/api/session":
            self._send_json(self.session.overview())
        elif parsed.path == "/api/job":
            self._send_json(self.session.job.status())
        elif parsed.path == "/api/history":
            machine = query.get("machine", [""])[0]
            entries = history_module.collect(self.store)

            if not machine and entries:
                # Default to the machine you are sitting at: a trend line that
                # silently mixes two machines is worse than no trend line.
                machine = entries[-1]["machine"]

            self._send_json(history_module.series(entries, machine))
        elif parsed.path == "/api/tags":
            self._send_json([{"name": row["name"], "description": row["description"]}
                             for row in self.store.tags()])
        elif parsed.path == "/api/outstanding":
            run_id = self.run_id
            self._send_json(review.outstanding(self.store, run_id) if run_id else [])
        elif parsed.path == "/api/failures":
            run_id = self.run_id
            self._send_json(review.failures(self.store, run_id) if run_id else [])
        elif parsed.path == "/image":
            self._image(query, thumbnail=False)
        elif parsed.path == "/thumbnail":
            self._image(query, thumbnail=True)
        elif parsed.path.startswith("/web/"):
            asset = WEB_FILES.get(parsed.path[len("/web/"):])

            if asset is None:
                self._send_json({"error": "not found"}, status=404)
            else:
                self._send_file(asset)
        else:
            self._send_json({"error": "not found"}, status=404)

    def _post(self) -> None:
        parsed = urlparse(self.path)
        payload = self._read_json()

        if parsed.path == "/api/stop-run":
            # Polite about there being nothing to stop: the button can be
            # pressed twice, and a second press is not an error.
            stopped = self.session.job.stop()
            self._send_json(
                {
                    "stopping": stopped,
                    "message": "stopping — ending what is running now"
                    if stopped
                    else "nothing is running",
                }
            )

            return

        if parsed.path == "/api/build":
            # No run row behind this one: a build verified nothing, and a run
            # with nothing under it would sit in the history looking like one
            # that did.
            targets = [str(value) for value in payload.get("targets", [])]
            unknown = [name for name in targets if name not in runner_module.BUILD_TARGETS]

            if unknown:
                self._send_json({"error": f"unknown target(s): {', '.join(unknown)}"}, status=400)

                return

            if not self.session.job.start_build(targets or None):
                self._send_json({"error": "something is already running"}, status=409)

                return

            self._send_json(self.session.job.status())

            return

        if parsed.path == "/api/restart-hub":
            # Refused rather than queued while a job is on: exec would take a
            # capture pass down with it, and the half-written run it left
            # behind would be a worse thing to explain than a stale hub.
            if self.session.job.running:
                self._send_json(
                    {"error": "something is running — stop it first"}, status=409
                )

                return

            restart_module.restart_now()
            self._send_json({"restarting": True})

            return

        if parsed.path == "/api/run-suites":
            requested = [str(value) for value in payload.get("suites", [])]
            unknown = [name for name in requested if suites_module.by_id(name) is None]

            if unknown:
                self._send_json({"error": f"unknown suite(s): {', '.join(unknown)}"}, status=400)

                return

            resolutions = tuple(payload.get("resolutions") or surfaces.DEFAULT_RESOLUTIONS)
            bad = [name for name in resolutions if name not in surfaces.SWEEP_GEOMETRIES]

            if bad:
                self._send_json({"error": f"unknown resolution(s): {', '.join(bad)}"}, status=400)

                return

            themes = tuple(payload.get("themes") or surfaces.DEFAULT_THEMES)
            bad = [name for name in themes if name not in surfaces.THEMES]

            if bad:
                self._send_json({"error": f"unknown theme(s): {', '.join(bad)}"}, status=400)

                return

            if not requested:
                self._send_json({"error": "choose at least one suite"}, status=400)

                return

            started = self.session.job.start(
                requested,
                mode=str(payload.get("mode", surfaces.FULL)),
                resolutions=resolutions,
                themes=themes,
                rebuild=bool(payload.get("rebuild", False)),
                run_id=payload.get("run_id"),
            )

            if not started:
                self._send_json({"error": "something is already running"}, status=409)

                return

            # Whatever the job produces is what the page should show next.
            self.session.select(None)
            self._send_json(self.session.job.status())
        elif parsed.path == "/api/launch":
            if self.session.job.running:
                # Two instances would fight over the audio device, and a capture
                # pass has a very definite opinion about which window is which.
                self._send_json(
                    {"error": "something is running — wait for it to finish"}, status=409
                )

                return

            capture = (
                self.store.capture(int(payload["capture_id"]))
                if payload.get("capture_id")
                else None
            )
            state = str(payload.get("state") or (capture.surface_state if capture else ""))
            geometry = str(payload.get("geometry") or (capture.geometry if capture else ""))
            theme = str(payload.get("theme") or (capture.theme if capture else ""))

            try:
                self._send_json(
                    self.session.launch.open(state=state, geometry=geometry, theme=theme)
                )
            except launcher_module.LaunchError as error:
                self._send_json({"error": str(error)}, status=400)
        elif parsed.path == "/api/launch/close":
            self._send_json(self.session.launch.close())
        elif parsed.path == "/api/sync":
            self._send_json(history_module.sync(self.store))
        elif parsed.path == "/api/select":
            self.session.select(payload.get("run_id"))
            self._send_json(self.session.overview())
        elif parsed.path == "/api/score":
            problems = review.score(
                self.store,
                int(payload.get("capture_id", 0)),
                str(payload.get("question", "")),
                str(payload.get("verdict", "")),
                str(payload.get("note", "")),
                attended=bool(payload.get("attended", False)),
            )
            self._send_json({"problems": problems}, status=200 if not problems else 400)
        elif parsed.path == "/api/score-many":
            ids = [int(value) for value in payload.get("capture_ids", [])]

            if not ids:
                self._send_json({"error": "select some images first"}, status=400)

                return

            result = review.score_many(
                self.store,
                ids,
                str(payload.get("verdict", "")),
                str(payload.get("note", "")),
                overwrite=bool(payload.get("overwrite", False)),
            )
            self._send_json(result, status=400 if result["problems"] else 200)
        elif parsed.path == "/api/tag":
            ids = [int(value) for value in payload.get("capture_ids", [])]
            name = str(payload.get("tag", "")).strip()

            if not name:
                self._send_json({"error": "a tag needs a name"}, status=400)

                return

            if payload.get("remove"):
                self._send_json(review.remove_tag(self.store, ids, name))
            else:
                self._send_json(review.apply_tag(self.store, ids, name))
        elif parsed.path == "/api/tags":
            name = str(payload.get("name", "")).strip()

            if not name:
                self._send_json({"error": "a tag needs a name"}, status=400)

                return

            self.store.add_tag(name, str(payload.get("description", "")))
            self._send_json({"name": name})
        elif parsed.path == "/api/comment":
            # One capture or many, through one endpoint: the page sends whichever
            # it has, and a selection of one behaves exactly like the old call.
            ids = payload.get("capture_ids")

            if ids:
                result = review.add_comment_to_many(
                    self.store, [int(value) for value in ids], str(payload.get("body", ""))
                )
            else:
                result = review.add_comment(
                    self.store, int(payload.get("capture_id", 0)), str(payload.get("body", ""))
                )

            self._send_json(result, status=400 if "error" in result else 200)
        elif parsed.path == "/api/delete-comment":
            result = review.delete_comment(self.store, int(payload.get("comment_id", 0)))
            self._send_json(result, status=400 if "error" in result else 200)
        else:
            self._send_json({"error": "not found"}, status=404)


def serve(store: Store, run_id: int | None = None, port: int = 8730) -> ThreadingHTTPServer:
    """The hub's server. The caller owns its lifetime.

    `run_id` may be None: an empty store is how this looks the first time, and
    the page offers to run something rather than refusing to open.
    """
    session = ReviewSession(store, run_id)
    handler = type("BoundReviewHandler", (ReviewHandler,), {"session": session})
    httpd = ThreadingHTTPServer((HOST, port), handler)
    httpd.session = session  # type: ignore[attr-defined]

    return httpd
