#!/usr/bin/env python3
"""Exercise the feedback schema backup and restore path without cloud credentials."""

from __future__ import annotations

import sqlite3
import tempfile
from pathlib import Path


def apply_migrations(database: sqlite3.Connection, migrations: Path) -> None:
    for migration in sorted(migrations.glob("*.sql")):
        database.executescript(migration.read_text(encoding="utf-8"))


def main() -> None:
    repository = Path(__file__).resolve().parents[2]
    migrations = repository / "services" / "feedback-intake" / "migrations"

    with tempfile.TemporaryDirectory(prefix="practice-takes-feedback-recovery-") as directory:
        source_path = Path(directory) / "source.sqlite3"
        restored_path = Path(directory) / "restored.sqlite3"

        with sqlite3.connect(source_path) as source:
            source.execute("PRAGMA foreign_keys = ON")
            apply_migrations(source, migrations)
            source.execute(
                """
                INSERT INTO feedback_submissions
                    (receipt_id, schema_version, submitted_at, received_at, app_version,
                     installation_hash, client_hash, category, message, contact_email,
                     client_submission_id)
                VALUES
                    ('11111111-1111-4111-8111-111111111111', 1,
                     '2026-07-23T12:00:00.000Z', 1784808000, '0.4.1',
                     'recovery-installation', 'recovery-client', 'bug',
                     'Recovery drill fixture', NULL,
                     '22222222-2222-4222-8222-222222222222')
                """
            )
            source.commit()
            with sqlite3.connect(restored_path) as restored:
                source.backup(restored)

        with sqlite3.connect(restored_path) as restored:
            integrity = restored.execute("PRAGMA integrity_check").fetchone()
            record = restored.execute(
                "SELECT receipt_id, message FROM feedback_submissions"
            ).fetchone()

        expected = (
            "11111111-1111-4111-8111-111111111111",
            "Recovery drill fixture",
        )
        if integrity != ("ok",) or record != expected:
            raise RuntimeError(
                f"Recovery verification failed: integrity={integrity!r}, record={record!r}"
            )

    print("Feedback recovery drill passed: schema, backup integrity, and fixture verified.")


if __name__ == "__main__":
    main()
