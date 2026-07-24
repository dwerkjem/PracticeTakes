ALTER TABLE feedback_submissions ADD COLUMN quarantine_reason TEXT;
ALTER TABLE feedback_submissions ADD COLUMN quarantined_at INTEGER;

CREATE INDEX feedback_submissions_quarantine_time
    ON feedback_submissions (quarantined_at DESC)
    WHERE quarantine_reason IS NOT NULL;

CREATE TABLE request_metrics (
    hour INTEGER NOT NULL,
    route TEXT NOT NULL CHECK (route IN ('authorizations', 'submissions')),
    outcome TEXT NOT NULL CHECK (outcome IN ('success', 'rejected', 'failure')),
    request_count INTEGER NOT NULL DEFAULT 0,
    total_duration_ms INTEGER NOT NULL DEFAULT 0,
    maximum_duration_ms INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (hour, route, outcome)
);

CREATE TABLE maintenance_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    operation TEXT NOT NULL CHECK (operation IN ('retention')),
    actor TEXT NOT NULL,
    started_at INTEGER NOT NULL,
    completed_at INTEGER NOT NULL,
    details_json TEXT NOT NULL DEFAULT '{}'
);

CREATE INDEX maintenance_runs_operation_time
    ON maintenance_runs (operation, completed_at DESC);
