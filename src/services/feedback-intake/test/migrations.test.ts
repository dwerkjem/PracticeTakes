import { describe, expect, it } from "vitest";

import { DatabaseSync } from "node:sqlite";

import { applyMigrations, createTestDatabase, migrationFileNames } from "./support/database";

describe("feedback database migrations", () => {
  it("discovers the migration set", () => {
    const names = migrationFileNames();
    expect(names.length).toBeGreaterThan(0);
    expect(names.every((name) => /^\d{4}_.+\.sql$/.test(name))).toBe(true);
  });

  it("numbers migrations contiguously from one", () => {
    const numbers = migrationFileNames().map((name) => Number.parseInt(name.slice(0, 4), 10));
    expect(numbers).toEqual(numbers.map((_, index) => index + 1));
  });

  it("applies every migration in order to an empty database", () => {
    const sqlite = new DatabaseSync(":memory:");
    expect(() => applyMigrations(sqlite)).not.toThrow();
    expect(applyMigrations(new DatabaseSync(":memory:"))).toEqual(migrationFileNames());
  });

  it("names the migration that fails to apply", () => {
    const alreadyMigrated = new DatabaseSync(":memory:");
    applyMigrations(alreadyMigrated);
    // Re-applying re-creates existing tables, so the first migration fails and
    // its name must reach the failure message.
    expect(() => applyMigrations(alreadyMigrated))
      .toThrowError(/Migration 0001_initial\.sql could not be applied/);
  });

  it("produces the schema the worker queries", () => {
    const database = createTestDatabase();
    const tables = database
      .rows<{ name: string }>("SELECT name FROM sqlite_master WHERE type = 'table'")
      .map((table) => table.name)
      .sort();

    expect(tables).toEqual([
      "admin_action_receipts",
      "authorization_requests",
      "consumed_authorizations",
      "feedback_email_queue",
      "feedback_notification_days",
      "feedback_submissions",
      "maintenance_runs",
      "request_metrics",
      "sqlite_sequence",
    ]);
  });

  it("enforces one submission per installation and client submission identifier", () => {
    const database = createTestDatabase();
    const insert = (clientSubmissionId: string | null, receiptId: string) =>
      database.execute(
        `INSERT INTO feedback_submissions
           (receipt_id, schema_version, submitted_at, received_at, app_version,
            installation_hash, client_hash, category, message, client_submission_id)
         VALUES (?, 1, '2026-07-19T02:00:00.000Z', 1768788000, '0.3.1',
                 'installation', 'client', 'bug', 'A report', ?)`,
        receiptId,
        clientSubmissionId,
      );

    insert("submission-one", "receipt-one");
    expect(() => insert("submission-one", "receipt-two"))
      .toThrowError(/UNIQUE constraint failed/);
    // The unique index is partial, so unidentified legacy rows still coexist.
    insert(null, "receipt-three");
    insert(null, "receipt-four");
    expect(database.count("feedback_submissions")).toBe(3);
  });

  it("lets the daily email count exceed the retired fixed maximum of three", () => {
    const database = createTestDatabase();
    const reserve = (count: number) =>
      database.execute(
        `INSERT INTO feedback_notification_days (notification_day, sent_count, updated_at)
         VALUES ('2026-07-24', ?, 1768788000)
         ON CONFLICT(notification_day) DO UPDATE SET sent_count = excluded.sent_count`,
        count,
      );

    // The original CHECK stopped at three, which would have rejected the
    // fourth email of a day whenever FEEDBACK_MAX_DAILY_EMAILS allowed one.
    reserve(9);
    expect(database.row<{ sent_count: number }>(
      "SELECT sent_count FROM feedback_notification_days WHERE notification_day = '2026-07-24'",
    )?.sent_count).toBe(9);
    expect(() => reserve(-1)).toThrowError(/CHECK constraint failed/);
  });

  it("records notification runs alongside retention runs and nothing else", () => {
    const database = createTestDatabase();
    const record = (operation: string) =>
      database.execute(
        `INSERT INTO maintenance_runs (operation, actor, started_at, completed_at)
         VALUES (?, 'scheduled', 1768788000, 1768788001)`,
        operation,
      );

    record("retention");
    record("notification");
    expect(() => record("something_else")).toThrowError(/CHECK constraint failed/);
    expect(database.count("maintenance_runs")).toBe(2);
  });

  it("keeps the maintenance history index the rebuild dropped", () => {
    const database = createTestDatabase();
    expect(database.rows<{ name: string }>(
      "SELECT name FROM sqlite_master WHERE type = 'index' AND tbl_name = 'maintenance_runs'",
    ).map((index) => index.name)).toContain("maintenance_runs_operation_time");
  });
});
