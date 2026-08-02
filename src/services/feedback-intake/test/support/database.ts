// A D1 stand-in backed by a real SQLite engine.
//
// The schema is built by applying the project's own migration files, so a query
// that references a column no migration creates fails here exactly as it would
// against D1. Nothing in this module inspects statement text to decide what to
// return: every statement is parsed and executed.

import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { DatabaseSync } from "node:sqlite";
import { fileURLToPath } from "node:url";

const migrationsDirectory = join(
  dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
  "migrations",
);

export function migrationFileNames(): string[] {
  return readdirSync(migrationsDirectory)
    .filter((name) => name.endsWith(".sql"))
    .sort();
}

export function applyMigrations(sqlite: DatabaseSync): string[] {
  const applied: string[] = [];
  for (const name of migrationFileNames()) {
    const statements = readFileSync(join(migrationsDirectory, name), "utf8");
    try {
      sqlite.exec(statements);
    } catch (error) {
      throw new Error(
        `Migration ${name} could not be applied: ${(error as Error).message}`,
        { cause: error },
      );
    }
    applied.push(name);
  }
  return applied;
}

function boundValue(value: unknown): unknown {
  if (value === undefined) {
    throw new TypeError(
      "D1 statements cannot bind undefined; bind null instead.",
    );
  }
  return typeof value === "boolean" ? Number(value) : value;
}

function meta(changes = 0, lastRowId = 0) {
  return {
    changed_db: changes > 0,
    changes,
    duration: 0,
    last_row_id: lastRowId,
    rows_read: 0,
    rows_written: changes,
    served_by: "test-sqlite",
    size_after: 0,
  };
}

class TestStatement {
  constructor(
    private readonly sqlite: DatabaseSync,
    readonly sql: string,
    private readonly parameters: unknown[] = [],
  ) {}

  bind(...parameters: unknown[]): TestStatement {
    return new TestStatement(this.sqlite, this.sql, parameters);
  }

  async first<T>(column?: string): Promise<T | null> {
    const row = this.sqlite.prepare(this.sql).get(
      ...this.parameters.map(boundValue) as never[],
    ) as Record<string, unknown> | undefined;
    if (row === undefined) return null;
    return (column === undefined ? row : row[column]) as T;
  }

  async all<T>(): Promise<{ success: true; results: T[]; meta: ReturnType<typeof meta> }> {
    const rows = this.sqlite.prepare(this.sql).all(
      ...this.parameters.map(boundValue) as never[],
    ) as T[];
    return { success: true, results: rows, meta: meta() };
  }

  async run<T>(): Promise<{ success: true; results: T[]; meta: ReturnType<typeof meta> }> {
    const result = this.sqlite.prepare(this.sql).run(
      ...this.parameters.map(boundValue) as never[],
    );
    return {
      success: true,
      results: [],
      meta: meta(Number(result.changes), Number(result.lastInsertRowid)),
    };
  }

  raw(): never {
    throw new Error(
      "TestDatabase does not implement D1PreparedStatement.raw(); add it here if the worker starts using it.",
    );
  }
}

export class TestDatabase {
  readonly sqlite = new DatabaseSync(":memory:");
  readonly appliedMigrations: string[];

  constructor() {
    this.appliedMigrations = applyMigrations(this.sqlite);
  }

  prepare(sql: string): TestStatement {
    return new TestStatement(this.sqlite, sql);
  }

  // D1 runs a batch as a single transaction, so a failure partway through must
  // leave none of the batch's rows behind.
  async batch(
    statements: TestStatement[],
  ): Promise<Awaited<ReturnType<TestStatement["run"]>>[]> {
    this.sqlite.exec("BEGIN");
    try {
      const results = [];
      for (const statement of statements) results.push(await statement.run());
      this.sqlite.exec("COMMIT");
      return results;
    } catch (error) {
      this.sqlite.exec("ROLLBACK");
      throw error;
    }
  }

  dump(): never {
    throw new Error("TestDatabase does not implement D1Database.dump().");
  }

  exec(): never {
    throw new Error("TestDatabase does not implement D1Database.exec().");
  }

  // Test-side inspection and seeding. These execute SQL like everything else,
  // so a helper that names a column the migrations do not define also fails.
  rows<T = Record<string, unknown>>(sql: string, ...parameters: unknown[]): T[] {
    return this.sqlite.prepare(sql).all(
      ...parameters.map(boundValue) as never[],
    ) as T[];
  }

  row<T = Record<string, unknown>>(sql: string, ...parameters: unknown[]): T | null {
    const row = this.sqlite.prepare(sql).get(
      ...parameters.map(boundValue) as never[],
    );
    return (row ?? null) as T | null;
  }

  count(table: string, where = "", ...parameters: unknown[]): number {
    const row = this.row<{ count: number }>(
      `SELECT COUNT(*) AS count FROM ${table} ${where ? `WHERE ${where}` : ""}`,
      ...parameters,
    );
    return Number(row?.count ?? 0);
  }

  execute(sql: string, ...parameters: unknown[]): void {
    this.sqlite.prepare(sql).run(...parameters.map(boundValue) as never[]);
  }
}

export function createTestDatabase(): TestDatabase {
  return new TestDatabase();
}

// Handed to worker code, which only ever sees the D1 surface.
export function asD1(database: TestDatabase): D1Database {
  return database as unknown as D1Database;
}

export interface SubmissionSeed {
  receiptId?: string;
  submittedAt?: string;
  receivedAt?: number;
  appVersion?: string;
  installationHash?: string;
  clientHash?: string;
  category?: string;
  message?: string;
  contactEmail?: string | null;
  status?: string;
  developerNotes?: string;
  priority?: string | null;
  tagsJson?: string;
  githubIssueUrl?: string | null;
  duplicateOf?: string | null;
  screenshotMimeType?: string | null;
  screenshotBase64?: string | null;
  clientSubmissionId?: string | null;
  quarantineReason?: string | null;
  quarantinedAt?: number | null;
}

export function seedSubmission(
  database: TestDatabase,
  seed: SubmissionSeed = {},
): string {
  const receiptId = seed.receiptId ?? crypto.randomUUID();
  database.execute(
    `INSERT INTO feedback_submissions
       (receipt_id, schema_version, submitted_at, received_at, app_version,
        installation_hash, client_hash, category, message, contact_email,
        status, developer_notes, priority, tags_json, github_issue_url,
        duplicate_of, screenshot_mime_type, screenshot_base64,
        client_submission_id, quarantine_reason, quarantined_at)
     VALUES (?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    receiptId,
    seed.submittedAt ?? "2026-07-19T02:00:00.000Z",
    seed.receivedAt ?? 1_768_788_000,
    seed.appVersion ?? "0.3.1",
    seed.installationHash ?? "seeded-installation",
    seed.clientHash ?? "seeded-client",
    seed.category ?? "bug",
    seed.message ??
      "Type: Bug\nTitle: Wrong note\n\nDescription:\nThe note is wrong.\nEnvironment: Practice Takes 0.3.1 | Linux",
    seed.contactEmail ?? null,
    seed.status ?? "new",
    seed.developerNotes ?? "",
    seed.priority ?? null,
    seed.tagsJson ?? "[]",
    seed.githubIssueUrl ?? null,
    seed.duplicateOf ?? null,
    seed.screenshotMimeType ?? null,
    seed.screenshotBase64 ?? null,
    seed.clientSubmissionId ?? null,
    seed.quarantineReason ?? null,
    seed.quarantinedAt ?? null,
  );
  return receiptId;
}

export function seedQueuedFeedback(
  database: TestDatabase,
  receiptId: string,
  receivedAt: number,
  overrides: {
    appVersion?: string;
    category?: string;
    message?: string;
    contactEmail?: string | null;
    screenshotMimeType?: string | null;
  } = {},
): void {
  database.execute(
    `INSERT INTO feedback_email_queue
       (receipt_id, received_at, app_version, category, message, contact_email,
        screenshot_mime_type)
     VALUES (?, ?, ?, ?, ?, ?, ?)`,
    receiptId,
    receivedAt,
    overrides.appVersion ?? "0.4.3",
    overrides.category ?? "bug",
    overrides.message ?? `Complete feedback for ${receiptId}`,
    overrides.contactEmail ?? `${receiptId}@example.com`,
    overrides.screenshotMimeType ?? null,
  );
}

export function seedAdminAction(
  database: TestDatabase,
  seed: {
    adminEmail: string;
    action: "create" | "update" | "delete";
    receiptId: string;
    detailsJson?: string;
    createdAt?: number;
  },
): void {
  database.execute(
    `INSERT INTO admin_action_receipts
       (admin_email, action, receipt_id, details_json, created_at)
     VALUES (?, ?, ?, ?, ?)`,
    seed.adminEmail,
    seed.action,
    seed.receiptId,
    seed.detailsJson ?? "{}",
    seed.createdAt ?? 1_768_788_000,
  );
}

export function seedRequestMetric(
  database: TestDatabase,
  seed: {
    hour: number;
    route: "authorizations" | "submissions";
    outcome: "success" | "rejected" | "failure";
    requestCount: number;
    totalDurationMs?: number;
    maximumDurationMs?: number;
  },
): void {
  database.execute(
    `INSERT INTO request_metrics
       (hour, route, outcome, request_count, total_duration_ms, maximum_duration_ms)
     VALUES (?, ?, ?, ?, ?, ?)`,
    seed.hour,
    seed.route,
    seed.outcome,
    seed.requestCount,
    seed.totalDurationMs ?? 0,
    seed.maximumDurationMs ?? 0,
  );
}

export function seedMaintenanceRun(
  database: TestDatabase,
  seed: { actor: string; completedAt: number; detailsJson?: string },
): void {
  database.execute(
    `INSERT INTO maintenance_runs
       (operation, actor, started_at, completed_at, details_json)
     VALUES ('retention', ?, ?, ?, ?)`,
    seed.actor,
    seed.completedAt,
    seed.completedAt,
    seed.detailsJson ?? "{}",
  );
}
