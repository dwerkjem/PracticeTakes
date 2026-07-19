CREATE TABLE authorization_requests (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    client_hash TEXT NOT NULL,
    requested_at INTEGER NOT NULL
);

CREATE INDEX authorization_requests_client_time
    ON authorization_requests (client_hash, requested_at);

CREATE TABLE consumed_authorizations (
    token_id TEXT PRIMARY KEY,
    consumed_at INTEGER NOT NULL
);

CREATE TABLE feedback_submissions (
    receipt_id TEXT PRIMARY KEY,
    schema_version INTEGER NOT NULL,
    submitted_at TEXT NOT NULL,
    received_at INTEGER NOT NULL,
    app_version TEXT NOT NULL,
    installation_hash TEXT NOT NULL,
    client_hash TEXT NOT NULL,
    category TEXT NOT NULL,
    message TEXT NOT NULL,
    contact_email TEXT
);

CREATE INDEX feedback_submissions_installation_time
    ON feedback_submissions (installation_hash, received_at);

CREATE INDEX feedback_submissions_client_time
    ON feedback_submissions (client_hash, received_at);
