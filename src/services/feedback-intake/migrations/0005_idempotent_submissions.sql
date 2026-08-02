ALTER TABLE feedback_submissions ADD COLUMN client_submission_id TEXT;
CREATE UNIQUE INDEX feedback_submissions_client_submission
    ON feedback_submissions (installation_hash, client_submission_id)
    WHERE client_submission_id IS NOT NULL;
