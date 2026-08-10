-- The daily send count was capped at three by a CHECK constraint written when
-- three was a constant. It is now FEEDBACK_MAX_DAILY_EMAILS, so the constraint
-- would reject the fourth reservation of a busy day. SQLite cannot alter a
-- CHECK, so both tables here are rebuilt rather than altered.

CREATE TABLE feedback_notification_days_rebuilt (
    notification_day TEXT PRIMARY KEY,
    sent_count INTEGER NOT NULL DEFAULT 0 CHECK (sent_count >= 0),
    updated_at INTEGER NOT NULL
);

INSERT INTO feedback_notification_days_rebuilt
    (notification_day, sent_count, updated_at)
SELECT notification_day, sent_count, updated_at
  FROM feedback_notification_days;

DROP TABLE feedback_notification_days;

ALTER TABLE feedback_notification_days_rebuilt
    RENAME TO feedback_notification_days;

-- Dispatch attempts are recorded alongside retention runs: same actor and
-- outcome shape, same indexes, same audit retention.

CREATE TABLE maintenance_runs_rebuilt (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    operation TEXT NOT NULL CHECK (operation IN ('retention', 'notification')),
    actor TEXT NOT NULL,
    started_at INTEGER NOT NULL,
    completed_at INTEGER NOT NULL,
    details_json TEXT NOT NULL DEFAULT '{}'
);

INSERT INTO maintenance_runs_rebuilt
    (id, operation, actor, started_at, completed_at, details_json)
SELECT id, operation, actor, started_at, completed_at, details_json
  FROM maintenance_runs;

DROP TABLE maintenance_runs;

ALTER TABLE maintenance_runs_rebuilt RENAME TO maintenance_runs;

CREATE INDEX maintenance_runs_operation_time
    ON maintenance_runs (operation, completed_at DESC);
