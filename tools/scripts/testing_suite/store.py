#!/usr/bin/env python3
"""The durable store behind a verification run.

One SQLite file holding every run: the machine it ran on, what was captured,
what a reviewer decided about each capture, what the Performance Lab measured,
and what the automated suites said. Having them in one place is the point — it
is what makes "did the tuner's presentation regress since June" a query rather
than an afternoon of reading files.

Two rules shape everything here:

- **The store is local history; the exported record is the contract.** The
  release gate reads records under docs/development/quality/manual-runs/, never
  this database. Losing this file costs comparison history and never a release,
  which is why it can live outside the repository and outside `build/`.
- **Images are files, not blobs.** A run is tens of megabytes of PNG. The store
  holds each image's path, size, and digest so a backup of the database stays
  small and the review server can serve a file directly.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass
import datetime as _datetime
import json
import os
from pathlib import Path
import sqlite3
import threading

# Bumped by adding a migration below, never by editing one that shipped.
SCHEMA_VERSION = 3

# The tag vocabulary a fresh store starts with. Data rather than a constant the
# code branches on: a reviewer adds to this during a review.
STARTING_TAGS = (
    ("broken", "Something is visibly wrong or missing."),
    ("ugly", "It works, but it does not look right."),
    ("illegible", "Text or detail cannot be read at this size."),
)

PASS = "pass"
FAIL = "fail"
SKIP = "skip"
VERDICTS = (PASS, FAIL, SKIP)


class StoreError(RuntimeError):
    """The store cannot be used as asked."""


def default_database_path() -> Path:
    """Where the store lives when nobody says otherwise.

    Under the XDG data directory rather than in `build/`: a clean build deletes
    `build/`, and history a clean build destroys is not history. Outside the
    repository too, because a binary database in git would conflict on every
    run and would quietly become the thing releases depend on.
    """
    root = os.environ.get("XDG_DATA_HOME", "").strip()
    base = Path(root) if root else Path.home() / ".local" / "share"

    return base / "practice-takes-testing-suite" / "verification.db"


def now() -> str:
    return _datetime.datetime.now().replace(microsecond=0).isoformat()


@dataclass(frozen=True)
class CaptureRow:
    """One surface at one resolution, with whatever is known about it."""

    id: int
    run_id: int
    surface_state: str
    surface_title: str
    geometry: str
    theme: str
    image_path: str
    thumbnail_path: str
    width: int
    height: int
    digest: str
    failure: str
    pruned: bool
    notice: str = ""

    @property
    def failed(self) -> bool:
        return bool(self.failure)


def _text(row: sqlite3.Row, name: str) -> str:
    try:
        return row[name] or ""
    except (IndexError, KeyError):
        return ""


def _number(row: sqlite3.Row, name: str) -> int:
    try:
        return int(row[name] or 0)
    except (IndexError, KeyError, TypeError, ValueError):
        return 0


def _capture_row(row: sqlite3.Row) -> CaptureRow:
    """One row, read defensively.

    A review tool that cannot render a row it can read is worse than one that
    renders it with an unknown size: the grid says "missing" and the reviewer
    moves on, rather than getting a traceback and a dead request.
    """
    return CaptureRow(
        id=_number(row, "id"),
        run_id=_number(row, "run_id"),
        surface_state=_text(row, "surface_state"),
        surface_title=_text(row, "surface_title"),
        geometry=_text(row, "geometry"),
        theme=_text(row, "theme") or "dark",
        image_path=_text(row, "image_path"),
        thumbnail_path=_text(row, "thumbnail_path"),
        width=_number(row, "width"),
        height=_number(row, "height"),
        digest=_text(row, "digest"),
        failure=_text(row, "failure"),
        pruned=bool(_number(row, "pruned")),
        notice=_text(row, "notice"),
    )


def _migration_1(connection: sqlite3.Connection) -> None:
    connection.executescript(
        """
        CREATE TABLE machine (
            id               INTEGER PRIMARY KEY,
            identity         TEXT NOT NULL UNIQUE,
            processor        TEXT NOT NULL,
            cores            INTEGER NOT NULL,
            memory_bytes     INTEGER NOT NULL,
            graphics         TEXT NOT NULL,
            operating_system TEXT NOT NULL,
            display          TEXT NOT NULL,
            first_seen       TEXT NOT NULL,
            last_seen        TEXT NOT NULL
        );

        CREATE TABLE run (
            id            INTEGER PRIMARY KEY,
            machine_id    INTEGER NOT NULL REFERENCES machine(id),
            commit_hash   TEXT NOT NULL,
            build_config  TEXT NOT NULL DEFAULT '',
            mode          TEXT NOT NULL,
            resolutions   TEXT NOT NULL DEFAULT '[]',
            platform      TEXT NOT NULL DEFAULT '',
            audio_device  TEXT NOT NULL DEFAULT '',
            attributes    TEXT NOT NULL DEFAULT '{}',
            started_at    TEXT NOT NULL,
            finished_at   TEXT NOT NULL DEFAULT '',
            complete      INTEGER NOT NULL DEFAULT 0
        );

        CREATE TABLE capture (
            id             INTEGER PRIMARY KEY,
            run_id         INTEGER NOT NULL REFERENCES run(id) ON DELETE CASCADE,
            surface_state  TEXT NOT NULL,
            surface_title  TEXT NOT NULL,
            geometry       TEXT NOT NULL,
            image_path     TEXT NOT NULL DEFAULT '',
            thumbnail_path TEXT NOT NULL DEFAULT '',
            width          INTEGER NOT NULL DEFAULT 0,
            height         INTEGER NOT NULL DEFAULT 0,
            digest         TEXT NOT NULL DEFAULT '',
            failure        TEXT NOT NULL DEFAULT '',
            pruned         INTEGER NOT NULL DEFAULT 0,
            captured_at    TEXT NOT NULL,
            UNIQUE (run_id, surface_state, surface_title, geometry)
        );

        CREATE TABLE axis_verdict (
            id          INTEGER PRIMARY KEY,
            capture_id  INTEGER NOT NULL REFERENCES capture(id) ON DELETE CASCADE,
            question    TEXT NOT NULL,
            prompt      TEXT NOT NULL,
            verdict     TEXT NOT NULL,
            note        TEXT NOT NULL DEFAULT '',
            attended    INTEGER NOT NULL DEFAULT 0,
            answered_at TEXT NOT NULL,
            UNIQUE (capture_id, question)
        );

        CREATE TABLE tag (
            id          INTEGER PRIMARY KEY,
            name        TEXT NOT NULL UNIQUE,
            description TEXT NOT NULL DEFAULT ''
        );

        CREATE TABLE capture_tag (
            capture_id INTEGER NOT NULL REFERENCES capture(id) ON DELETE CASCADE,
            tag_id     INTEGER NOT NULL REFERENCES tag(id) ON DELETE CASCADE,
            applied_at TEXT NOT NULL,
            PRIMARY KEY (capture_id, tag_id)
        );

        CREATE TABLE comment (
            id         INTEGER PRIMARY KEY,
            capture_id INTEGER NOT NULL REFERENCES capture(id) ON DELETE CASCADE,
            body       TEXT NOT NULL,
            written_at TEXT NOT NULL
        );

        CREATE TABLE measurement (
            id          INTEGER PRIMARY KEY,
            run_id      INTEGER NOT NULL REFERENCES run(id) ON DELETE CASCADE,
            metric      TEXT NOT NULL,
            value       REAL NOT NULL,
            unit        TEXT NOT NULL DEFAULT '',
            scenario    TEXT NOT NULL DEFAULT '',
            source      TEXT NOT NULL DEFAULT '',
            recorded_at TEXT NOT NULL
        );

        CREATE TABLE test_result (
            id               INTEGER PRIMARY KEY,
            run_id           INTEGER NOT NULL REFERENCES run(id) ON DELETE CASCADE,
            suite            TEXT NOT NULL,
            cases            INTEGER NOT NULL DEFAULT 0,
            failures         INTEGER NOT NULL DEFAULT 0,
            duration_seconds REAL NOT NULL DEFAULT 0,
            summary          TEXT NOT NULL DEFAULT '',
            recorded_at      TEXT NOT NULL,
            UNIQUE (run_id, suite)
        );

        CREATE INDEX capture_by_run ON capture (run_id);
        CREATE INDEX measurement_by_metric ON measurement (metric);
        """
    )

    for name, description in STARTING_TAGS:
        connection.execute(
            "INSERT INTO tag (name, description) VALUES (?, ?)", (name, description)
        )


def _migration_2(connection: sqlite3.Connection) -> None:
    """Something worth saying about a capture that is not a failure.

    Added because "this image is identical to another surface's" started life as
    a capture failure and threw away perfectly good images: some surfaces really
    do look identical — the settings window's default panel *is* the appearance
    panel. It is a hint for the reviewer, not a verdict.
    """
    connection.execute("ALTER TABLE capture ADD COLUMN notice TEXT NOT NULL DEFAULT ''")


def _migration_3(connection: sqlite3.Connection) -> None:
    """A capture is now a surface at a resolution *in a palette*.

    The theme has to be part of what makes a capture unique, or the light and
    dark captures of one surface overwrite each other -- so this rebuilds the
    table rather than adding a column, since SQLite cannot alter a UNIQUE
    constraint in place. Existing rows predate palettes and are recorded as
    dark, which is what they were.

    **The rebuild is one transaction.** `executescript` commits between
    statements, so without an explicit BEGIN another process reading the store --
    a hub someone left open in the other terminal -- can catch it between the
    DROP and the RENAME and read a table that is half of each. That produced a
    500 on an image request with a NULL width, which is the only symptom such a
    race is ever going to show.

    **Foreign keys are off for the rebuild, and that is the whole trick.**
    Verdicts, tags, and comments reference capture(id) ON DELETE CASCADE, so
    dropping the old table with enforcement on deletes every one of them. The
    first version of this migration did exactly that and threw away a run's
    entire review. Ids are preserved, so the children are still correct
    afterwards -- which `foreign_key_check` confirms before this returns.
    """
    # PRAGMA foreign_keys is a silent no-op inside a transaction, and an earlier
    # migration in the same open() will have left one running. Committing first
    # is what makes the pragma take; reading it back is what stops this failing
    # silently again if that ever stops being true.
    connection.commit()
    connection.execute("PRAGMA foreign_keys = OFF")

    if connection.execute("PRAGMA foreign_keys").fetchone()[0]:
        raise StoreError(
            "could not disable foreign keys to rebuild the capture table; refusing to "
            "continue, because doing so would delete every verdict, tag, and comment"
        )

    connection.executescript(
        """
        BEGIN IMMEDIATE;

        CREATE TABLE capture_v3 (
            id             INTEGER PRIMARY KEY,
            run_id         INTEGER NOT NULL REFERENCES run(id) ON DELETE CASCADE,
            surface_state  TEXT NOT NULL,
            surface_title  TEXT NOT NULL,
            geometry       TEXT NOT NULL,
            theme          TEXT NOT NULL DEFAULT 'dark',
            image_path     TEXT NOT NULL DEFAULT '',
            thumbnail_path TEXT NOT NULL DEFAULT '',
            width          INTEGER NOT NULL DEFAULT 0,
            height         INTEGER NOT NULL DEFAULT 0,
            digest         TEXT NOT NULL DEFAULT '',
            failure        TEXT NOT NULL DEFAULT '',
            notice         TEXT NOT NULL DEFAULT '',
            pruned         INTEGER NOT NULL DEFAULT 0,
            captured_at    TEXT NOT NULL,
            UNIQUE (run_id, surface_state, surface_title, geometry, theme)
        );

        INSERT INTO capture_v3
            (id, run_id, surface_state, surface_title, geometry, theme, image_path,
             thumbnail_path, width, height, digest, failure, notice, pruned, captured_at)
        SELECT id, run_id, surface_state, surface_title, geometry, 'dark', image_path,
               thumbnail_path, width, height, digest, failure, notice, pruned, captured_at
        FROM capture;

        DROP TABLE capture;
        ALTER TABLE capture_v3 RENAME TO capture;
        CREATE INDEX capture_by_run ON capture (run_id);

        COMMIT;
        """
    )

    broken = connection.execute("PRAGMA foreign_key_check").fetchall()
    connection.execute("PRAGMA foreign_keys = ON")

    if broken:
        raise StoreError(
            f"migrating the capture table orphaned {len(broken)} row(s); the store was "
            f"left unchanged"
        )


# Forward only. The recovery path for a bad migration is a copy of the file,
# which is one `cp` because the store is one file — cheaper and more honest than
# maintaining down-migrations nobody exercises.
MIGRATIONS = (_migration_1, _migration_2, _migration_3)


class Store:
    """A verification store. Open it, use it, close it."""

    def __init__(self, connection: sqlite3.Connection, path: Path) -> None:
        self.connection = connection
        self.path = path

        # The review server threads its requests, so one browser page load hits
        # this connection from several threads at once. The connection is opened
        # with check_same_thread off and every statement goes through this lock;
        # sharing one connection without it is the classic way to get sporadic
        # "recursive use of cursors" failures under exactly that load.
        self._lock = threading.RLock()

    def execute(self, sql: str, parameters: tuple = ()) -> sqlite3.Cursor:
        with self._lock:
            return self.connection.execute(sql, parameters)

    def execute_many(self, sql: str, parameters: list) -> sqlite3.Cursor:
        with self._lock:
            return self.connection.executemany(sql, parameters)

    def commit(self) -> None:
        with self._lock:
            self.connection.commit()

    # --- Lifecycle ----------------------------------------------------------

    @classmethod
    def open(cls, path: Path | None = None) -> "Store":
        path = Path(path) if path is not None else default_database_path()
        path.parent.mkdir(parents=True, exist_ok=True)

        connection = sqlite3.connect(path, check_same_thread=False)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        connection.execute("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)")

        store = cls(connection, path)
        store._migrate()

        return store

    def close(self) -> None:
        with self._lock:
            self.connection.close()

    def __enter__(self) -> "Store":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _current_version(self) -> int:
        row = self.execute("SELECT MAX(version) AS version FROM schema_version").fetchone()

        return int(row["version"]) if row and row["version"] is not None else 0

    def _migrate(self) -> None:
        version = self._current_version()

        if version > SCHEMA_VERSION:
            self.connection.close()
            raise StoreError(
                f"{self.path} was written by a newer testing suite (schema {version}; "
                f"this one understands {SCHEMA_VERSION}). Update the suite rather than "
                f"writing into a schema it does not understand."
            )

        for index in range(version, SCHEMA_VERSION):
            MIGRATIONS[index](self.connection)
            self.execute("INSERT INTO schema_version (version) VALUES (?)", (index + 1,))

        self.commit()

    # --- Machines -----------------------------------------------------------

    def machine_id(self, provenance: dict) -> int:
        """The machine row for this provenance, created on first sight."""
        identity = provenance["identity"]
        stamp = now()
        row = self.execute(
            "SELECT id FROM machine WHERE identity = ?", (identity,)
        ).fetchone()

        if row is not None:
            self.execute(
                "UPDATE machine SET last_seen = ? WHERE id = ?", (stamp, row["id"])
            )
            self.commit()

            return int(row["id"])

        cursor = self.execute(
            """
            INSERT INTO machine
                (identity, processor, cores, memory_bytes, graphics, operating_system,
                 display, first_seen, last_seen)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                identity,
                provenance.get("processor", ""),
                int(provenance.get("cores", 0)),
                int(provenance.get("memory_bytes", 0)),
                provenance.get("graphics", ""),
                provenance.get("operating_system", ""),
                provenance.get("display", ""),
                stamp,
                stamp,
            ),
        )
        self.commit()

        return int(cursor.lastrowid)

    def machine(self, machine_id: int) -> sqlite3.Row:
        row = self.execute("SELECT * FROM machine WHERE id = ?", (machine_id,)).fetchone()

        if row is None:
            raise StoreError(f"No machine {machine_id} in {self.path}.")

        return row

    # --- Runs ---------------------------------------------------------------

    def start_run(
        self,
        *,
        provenance: dict,
        commit: str,
        mode: str,
        resolutions: tuple[str, ...],
        build_config: str = "",
        platform: str = "",
        audio_device: str = "",
    ) -> int:
        cursor = self.execute(
            """
            INSERT INTO run
                (machine_id, commit_hash, build_config, mode, resolutions, platform,
                 audio_device, attributes, started_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                self.machine_id(provenance),
                commit,
                build_config,
                mode,
                json.dumps(list(resolutions)),
                platform,
                audio_device,
                json.dumps(provenance.get("attributes", {}), sort_keys=True),
                now(),
            ),
        )
        self.commit()

        return int(cursor.lastrowid)

    def run(self, run_id: int) -> sqlite3.Row:
        row = self.execute("SELECT * FROM run WHERE id = ?", (run_id,)).fetchone()

        if row is None:
            raise StoreError(f"No run {run_id} in {self.path}.")

        return row

    def runs(self) -> list[sqlite3.Row]:
        return list(
            self.execute("SELECT * FROM run ORDER BY started_at DESC, id DESC").fetchall()
        )

    def latest_run(self) -> sqlite3.Row | None:
        rows = self.runs()

        return rows[0] if rows else None

    def finish_run(self, run_id: int, *, complete: bool) -> None:
        self.execute(
            "UPDATE run SET finished_at = ?, complete = ? WHERE id = ?",
            (now(), 1 if complete else 0, run_id),
        )
        self.commit()

    def set_audio_device(self, run_id: int, device: str) -> None:
        self.execute("UPDATE run SET audio_device = ? WHERE id = ?", (device, run_id))
        self.commit()

    # --- Captures -----------------------------------------------------------

    def record_capture(
        self,
        run_id: int,
        *,
        state: str,
        title: str,
        geometry: str,
        theme: str = "dark",
        image_path: str = "",
        thumbnail_path: str = "",
        width: int = 0,
        height: int = 0,
        digest: str = "",
        failure: str = "",
        notice: str = "",
    ) -> int:
        """Store one capture, or the reason there is no image for it."""
        cursor = self.execute(
            """
            INSERT INTO capture
                (run_id, surface_state, surface_title, geometry, theme, image_path,
                 thumbnail_path, width, height, digest, failure, notice, captured_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT (run_id, surface_state, surface_title, geometry, theme) DO UPDATE SET
                image_path = excluded.image_path,
                thumbnail_path = excluded.thumbnail_path,
                width = excluded.width,
                height = excluded.height,
                digest = excluded.digest,
                failure = excluded.failure,
                notice = excluded.notice,
                pruned = 0,
                captured_at = excluded.captured_at
            """,
            (
                run_id,
                state,
                title,
                geometry,
                theme,
                image_path,
                thumbnail_path,
                width,
                height,
                digest,
                failure,
                notice,
                now(),
            ),
        )
        self.commit()

        if cursor.lastrowid:
            return int(cursor.lastrowid)

        return int(self.capture_id(run_id, state, title, geometry, theme) or 0)

    def capture_id(
        self, run_id: int, state: str, title: str, geometry: str, theme: str = "dark"
    ) -> int | None:
        row = self.execute(
            """
            SELECT id FROM capture
            WHERE run_id = ? AND surface_state = ? AND surface_title = ? AND geometry = ?
              AND theme = ?
            """,
            (run_id, state, title, geometry, theme),
        ).fetchone()

        return int(row["id"]) if row else None

    def capture(self, capture_id: int) -> CaptureRow | None:
        row = self.execute(
            "SELECT * FROM capture WHERE id = ?", (capture_id,)
        ).fetchone()

        return _capture_row(row) if row is not None else None

    def captures(self, run_id: int) -> list[CaptureRow]:
        rows = self.execute(
            "SELECT * FROM capture WHERE run_id = ? ORDER BY id", (run_id,)
        ).fetchall()

        return [_capture_row(row) for row in rows]

    def captured_keys(self, run_id: int) -> set[tuple[str, str, str, str]]:
        """What a resumed capture pass does not need to capture again."""
        return {
            (capture.surface_state, capture.surface_title, capture.geometry, capture.theme)
            for capture in self.captures(run_id)
            if capture.image_path or capture.failure
        }

    # --- Verdicts -----------------------------------------------------------

    def record_verdict(
        self,
        capture_id: int,
        *,
        question: str,
        prompt: str,
        verdict: str,
        note: str = "",
        attended: bool = False,
    ) -> list[str]:
        """Score one question. Returns problems, and stores nothing when there are any.

        A note is encouraged on a failure and not required. It was required
        once: a failure with no detail says something is wrong without saying
        what, so the surface has to be looked at again to learn anything. In
        practice that blocked the reviewer mid-pass over something they could
        see perfectly well, so the prompt stays and the refusal does not.
        """
        problems: list[str] = []

        if verdict not in VERDICTS:
            problems.append(f"'{verdict}' is not one of {', '.join(VERDICTS)}.")

        if problems:
            return problems

        self.execute(
            """
            INSERT INTO axis_verdict
                (capture_id, question, prompt, verdict, note, attended, answered_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT (capture_id, question) DO UPDATE SET
                prompt = excluded.prompt,
                verdict = excluded.verdict,
                note = excluded.note,
                attended = excluded.attended,
                answered_at = excluded.answered_at
            """,
            (capture_id, question, prompt, verdict, note, 1 if attended else 0, now()),
        )
        self.commit()

        return []

    def verdicts(self, capture_id: int) -> list[sqlite3.Row]:
        return list(
            self.execute(
                "SELECT * FROM axis_verdict WHERE capture_id = ? ORDER BY id", (capture_id,)
            ).fetchall()
        )

    def verdicts_for_run(self, run_id: int) -> list[sqlite3.Row]:
        return list(
            self.execute(
                """
                SELECT v.*, c.surface_state, c.surface_title, c.geometry
                FROM axis_verdict v
                JOIN capture c ON c.id = v.capture_id
                WHERE c.run_id = ?
                ORDER BY c.id, v.id
                """,
                (run_id,),
            ).fetchall()
        )

    # --- Tags ---------------------------------------------------------------

    def tags(self) -> list[sqlite3.Row]:
        return list(self.execute("SELECT * FROM tag ORDER BY name").fetchall())

    def add_tag(self, name: str, description: str = "") -> int:
        name = name.strip()

        if not name:
            raise StoreError("A tag needs a name.")

        self.execute(
            "INSERT OR IGNORE INTO tag (name, description) VALUES (?, ?)", (name, description)
        )
        self.commit()
        row = self.execute("SELECT id FROM tag WHERE name = ?", (name,)).fetchone()

        return int(row["id"])

    def apply_tag(self, capture_ids: list[int], tag_name: str) -> int:
        """Tag every capture in the selection. Returns how many now carry it."""
        tag_id = self.add_tag(tag_name)
        stamp = now()

        self.execute_many(
            "INSERT OR IGNORE INTO capture_tag (capture_id, tag_id, applied_at) VALUES (?, ?, ?)",
            [(capture_id, tag_id, stamp) for capture_id in capture_ids],
        )
        self.commit()

        return len(capture_ids)

    def remove_tag(self, capture_ids: list[int], tag_name: str) -> None:
        row = self.execute("SELECT id FROM tag WHERE name = ?", (tag_name,)).fetchone()

        if row is None:
            return

        self.execute_many(
            "DELETE FROM capture_tag WHERE capture_id = ? AND tag_id = ?",
            [(capture_id, int(row["id"])) for capture_id in capture_ids],
        )
        self.commit()

    def tags_for(self, capture_id: int) -> list[str]:
        return [
            row["name"]
            for row in self.execute(
                """
                SELECT t.name FROM capture_tag ct
                JOIN tag t ON t.id = ct.tag_id
                WHERE ct.capture_id = ?
                ORDER BY t.name
                """,
                (capture_id,),
            ).fetchall()
        ]

    # --- Comments -----------------------------------------------------------

    def add_comment(self, capture_id: int, body: str) -> int:
        body = body.strip()

        if not body:
            raise StoreError("A comment needs some text.")

        cursor = self.execute(
            "INSERT INTO comment (capture_id, body, written_at) VALUES (?, ?, ?)",
            (capture_id, body, now()),
        )
        self.commit()

        return int(cursor.lastrowid)

    def delete_comment(self, comment_id: int) -> bool:
        """Remove one comment. False when there was nothing to remove.

        A comment is a note somebody typed, not a record of what the build did,
        so it is deleted outright rather than marked. Nothing downstream reads
        comments -- the export carries verdicts and measurements -- so there is
        nothing for a tombstone to protect.
        """
        cursor = self.execute("DELETE FROM comment WHERE id = ?", (int(comment_id),))
        self.commit()

        return cursor.rowcount > 0

    def comments_for(self, capture_id: int) -> list[sqlite3.Row]:
        return list(
            self.execute(
                "SELECT * FROM comment WHERE capture_id = ? ORDER BY id", (capture_id,)
            ).fetchall()
        )

    # --- Measurements and automated results ---------------------------------

    def record_measurements(self, run_id: int, measurements: list[dict], source: str) -> int:
        """Store a whole ingested set, or none of it.

        All-or-nothing on purpose: a half-ingested export leaves a run looking
        like it measured less than it did, which is worse than a failed ingest.
        """
        stamp = now()

        try:
            with self._lock, self.connection:
                self.execute_many(
                    """
                    INSERT INTO measurement (run_id, metric, value, unit, scenario, source, recorded_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                    """,
                    [
                        (
                            run_id,
                            entry["metric"],
                            float(entry["value"]),
                            entry.get("unit", ""),
                            entry.get("scenario", ""),
                            source,
                            stamp,
                        )
                        for entry in measurements
                    ],
                )
        except (KeyError, TypeError, ValueError, sqlite3.Error) as error:
            raise StoreError(f"the measurement set was not stored: {error}") from error

        return len(measurements)

    def measurements(self, run_id: int) -> list[sqlite3.Row]:
        return list(
            self.execute(
                "SELECT * FROM measurement WHERE run_id = ? ORDER BY metric, id", (run_id,)
            ).fetchall()
        )

    def record_test_result(
        self,
        run_id: int,
        *,
        suite: str,
        cases: int,
        failures: int,
        duration_seconds: float = 0.0,
        summary: str = "",
    ) -> None:
        self.execute(
            """
            INSERT INTO test_result
                (run_id, suite, cases, failures, duration_seconds, summary, recorded_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT (run_id, suite) DO UPDATE SET
                cases = excluded.cases,
                failures = excluded.failures,
                duration_seconds = excluded.duration_seconds,
                summary = excluded.summary,
                recorded_at = excluded.recorded_at
            """,
            (run_id, suite, cases, failures, duration_seconds, summary, now()),
        )
        self.commit()

    def test_results(self, run_id: int) -> list[sqlite3.Row]:
        return list(
            self.execute(
                "SELECT * FROM test_result WHERE run_id = ? ORDER BY suite", (run_id,)
            ).fetchall()
        )

    # --- Comparison ---------------------------------------------------------

    def compare_metric(self, run_id: int, metric: str) -> dict:
        """This run's value for a metric against earlier runs on the same machine.

        Deliberately never crosses machines. A launch time from a different
        processor is not a baseline, and presenting it as one would make every
        comparison quietly meaningless.
        """
        run = self.run(run_id)
        current_row = self.execute(
            "SELECT value, unit FROM measurement WHERE run_id = ? AND metric = ? ORDER BY id DESC",
            (run_id, metric),
        ).fetchone()

        if current_row is None:
            return {"metric": metric, "current": None, "baseline": None, "reason": "not measured in this run"}

        earlier = self.execute(
            """
            SELECT m.value AS value, r.started_at AS started_at
            FROM measurement m
            JOIN run r ON r.id = m.run_id
            WHERE m.metric = ? AND r.machine_id = ? AND r.id != ? AND r.started_at <= ?
            ORDER BY r.started_at DESC, m.id DESC
            """,
            (metric, int(run["machine_id"]), run_id, run["started_at"]),
        ).fetchall()

        current = float(current_row["value"])

        if not earlier:
            return {
                "metric": metric,
                "unit": current_row["unit"],
                "current": current,
                "baseline": None,
                "reason": "no baseline on this machine",
            }

        baseline = float(earlier[0]["value"])

        return {
            "metric": metric,
            "unit": current_row["unit"],
            "current": current,
            "baseline": baseline,
            "baseline_started_at": earlier[0]["started_at"],
            "delta": current - baseline,
            "samples": len(earlier),
        }

    # --- Pruning ------------------------------------------------------------

    def prune_images(self, keep: int) -> list[str]:
        """Delete image files for all but the newest `keep` runs.

        Verdicts, tags, comments, measurements, and results stay. What is lost
        is the pixels, which are reproducible; what is kept is what somebody
        decided, which is not.
        """
        if keep < 0:
            raise StoreError("keep must not be negative.")

        run_ids = [int(row["id"]) for row in self.runs()][keep:]
        removed: list[str] = []

        for run_id in run_ids:
            for capture in self.captures(run_id):
                if capture.pruned:
                    continue

                for candidate in (capture.image_path, capture.thumbnail_path):
                    if not candidate:
                        continue

                    path = Path(candidate)

                    if path.exists():
                        path.unlink()
                        removed.append(candidate)

                self.execute(
                    "UPDATE capture SET pruned = 1, image_path = '', thumbnail_path = '' WHERE id = ?",
                    (capture.id,),
                )

        self.commit()

        return removed
