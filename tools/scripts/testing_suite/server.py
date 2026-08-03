#!/usr/bin/env python3
"""The HTTP layer under the grid review. Deliberately the thinnest part.

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
import mimetypes
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import review
from store import Store

WEB_ROOT = Path(__file__).resolve().parent / "web"
HOST = "127.0.0.1"


class ReviewHandler(BaseHTTPRequestHandler):
    """One review session's requests. The store is attached by `serve`."""

    store: Store
    run_id: int

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
        try:
            body = path.read_bytes()
        except OSError:
            self._send_json({"error": f"{path.name} is not readable"}, status=404)

            return

        kind, _ = mimetypes.guess_type(str(path))
        self.send_response(status)
        self.send_header("Content-Type", kind or "application/octet-stream")
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

        self._send_file(Path(capture.thumbnail_path if thumbnail else capture.image_path))

    # --- Routes -------------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler's name
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)

        if parsed.path in ("/", "/index.html"):
            self._send_file(WEB_ROOT / "index.html")
        elif parsed.path == "/api/run":
            self._send_json(review.run_view(self.store, self.run_id))
        elif parsed.path == "/api/tags":
            self._send_json([{"name": row["name"], "description": row["description"]}
                             for row in self.store.tags()])
        elif parsed.path == "/api/outstanding":
            self._send_json(review.outstanding(self.store, self.run_id))
        elif parsed.path == "/api/failures":
            self._send_json(review.failures(self.store, self.run_id))
        elif parsed.path == "/image":
            self._image(query, thumbnail=False)
        elif parsed.path == "/thumbnail":
            self._image(query, thumbnail=True)
        elif parsed.path.startswith("/web/"):
            candidate = (WEB_ROOT / parsed.path[len("/web/"):]).resolve()

            if WEB_ROOT.resolve() in candidate.parents and candidate.is_file():
                self._send_file(candidate)
            else:
                self._send_json({"error": "not found"}, status=404)
        else:
            self._send_json({"error": "not found"}, status=404)

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler's name
        parsed = urlparse(self.path)
        payload = self._read_json()

        if parsed.path == "/api/score":
            problems = review.score(
                self.store,
                int(payload.get("capture_id", 0)),
                str(payload.get("question", "")),
                str(payload.get("verdict", "")),
                str(payload.get("note", "")),
                attended=bool(payload.get("attended", False)),
            )
            self._send_json({"problems": problems}, status=200 if not problems else 400)
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
            result = review.add_comment(
                self.store, int(payload.get("capture_id", 0)), str(payload.get("body", ""))
            )
            self._send_json(result, status=400 if "error" in result else 200)
        else:
            self._send_json({"error": "not found"}, status=404)


def serve(store: Store, run_id: int, port: int = 8730) -> ThreadingHTTPServer:
    """A review server for one run. The caller owns its lifetime."""
    handler = type("BoundReviewHandler", (ReviewHandler,), {"store": store, "run_id": run_id})

    return ThreadingHTTPServer((HOST, port), handler)
