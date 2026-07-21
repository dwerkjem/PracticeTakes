ALTER TABLE feedback_submissions
    ADD COLUMN status TEXT NOT NULL DEFAULT 'new'
    CHECK (status IN ('new', 'needs_review', 'planned', 'duplicate', 'resolved', 'declined'));

ALTER TABLE feedback_submissions ADD COLUMN developer_notes TEXT NOT NULL DEFAULT '';
ALTER TABLE feedback_submissions ADD COLUMN priority TEXT CHECK (priority IN ('low', 'medium', 'high', 'critical'));
ALTER TABLE feedback_submissions ADD COLUMN tags_json TEXT NOT NULL DEFAULT '[]';
ALTER TABLE feedback_submissions ADD COLUMN github_issue_url TEXT;
ALTER TABLE feedback_submissions ADD COLUMN duplicate_of TEXT REFERENCES feedback_submissions(receipt_id);

CREATE INDEX feedback_submissions_status_time
    ON feedback_submissions (status, received_at DESC);

CREATE INDEX feedback_submissions_category_version
    ON feedback_submissions (category, app_version);

