CREATE TABLE admin_action_receipts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    admin_email TEXT NOT NULL,
    action TEXT NOT NULL CHECK (action IN ('create', 'update', 'delete')),
    receipt_id TEXT NOT NULL,
    details_json TEXT NOT NULL DEFAULT '{}',
    created_at INTEGER NOT NULL
);

CREATE INDEX admin_action_receipts_time
    ON admin_action_receipts (created_at DESC);

CREATE INDEX admin_action_receipts_admin_time
    ON admin_action_receipts (admin_email, created_at DESC);

CREATE INDEX admin_action_receipts_receipt
    ON admin_action_receipts (receipt_id, created_at DESC);

