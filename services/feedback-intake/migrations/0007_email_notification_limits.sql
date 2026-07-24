CREATE TABLE feedback_email_queue (
    receipt_id TEXT PRIMARY KEY,
    received_at INTEGER NOT NULL,
    app_version TEXT NOT NULL,
    category TEXT NOT NULL,
    message TEXT NOT NULL,
    contact_email TEXT,
    screenshot_mime_type TEXT,
    claim_id TEXT,
    claimed_at INTEGER
);

CREATE INDEX feedback_email_queue_pending
    ON feedback_email_queue (received_at ASC)
    WHERE claim_id IS NULL;

INSERT INTO feedback_email_queue
    (receipt_id, received_at, app_version, category, message, contact_email,
     screenshot_mime_type)
SELECT receipt_id, received_at, app_version, category, message, contact_email,
       screenshot_mime_type
  FROM feedback_submissions;

CREATE TABLE IF NOT EXISTS feedback_notification_days (
    notification_day TEXT PRIMARY KEY,
    sent_count INTEGER NOT NULL DEFAULT 0 CHECK (sent_count BETWEEN 0 AND 3),
    updated_at INTEGER NOT NULL
);
