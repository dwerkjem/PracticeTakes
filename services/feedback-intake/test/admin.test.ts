import { describe, expect, it } from "vitest";

import { handleAdminRequest } from "../src/admin";

const email = "developer@example.com";
const receiptId = "11111111-1111-4111-8111-111111111111";

const row = {
  receipt_id: receiptId,
  submitted_at: "2026-07-19T02:00:00.000Z",
  received_at: 1_768_788_000,
  app_version: "0.3.1",
  category: "bug",
  message: "Type: Bug\nTitle: Wrong note\n\nDescription:\nThe note is wrong.\nEnvironment: Practice Takes 0.3.1 | Linux",
  contact_email: null,
  status: "new",
  developer_notes: "",
  priority: null,
  tags_json: "[]",
  github_issue_url: null,
  duplicate_of: null,
  screenshot_mime_type: null,
  quarantine_reason: null,
  quarantined_at: null,
};

class AdminStatement {
  parameters: unknown[] = [];

  constructor(readonly database: AdminD1, readonly sql: string) {}

  bind(...parameters: unknown[]): AdminStatement {
    this.parameters = parameters;
    this.database.lastParameters = parameters;
    this.database.parameterHistory.push(parameters);
    return this;
  }

  async all<T>(): Promise<D1Result<T>> {
    this.database.lastSql = this.sql;
    this.database.sqlHistory.push(this.sql);
    const results = this.sql.includes("FROM admin_action_receipts")
      ? [{ id: 1, admin_email: email, action: "update", receipt_id: receiptId,
           details_json: '{"fields":["status"]}', created_at: 1_768_788_000 }]
      : [row];
    return { success: true, results: results as T[], meta: {} } as D1Result<T>;
  }

  async first<T>(): Promise<T | null> {
    this.database.lastSql = this.sql;
    this.database.sqlHistory.push(this.sql);
    if (this.sql.includes("FROM request_metrics")) {
      return {
        requests: 20,
        failures: 1,
        rejected: 2,
        total_duration_ms: 1000,
        maximum_duration_ms: 150,
      } as T;
    }
    if (this.sql.includes("strftime")) {
      return { average_seconds: 2, maximum_seconds: 5 } as T;
    }
    if (this.sql.includes("FROM maintenance_runs")) {
      return { completed_at: 1_768_788_000, details_json: '{"resolved":1}' } as T;
    }
    return { count: 10, bytes: 4096, quarantined: 1 } as T;
  }

  async run(): Promise<D1Result> {
    this.database.lastSql = this.sql;
    this.database.sqlHistory.push(this.sql);
    return { success: true, meta: { changes: this.database.updateChanges } } as D1Result;
  }
}

class AdminD1 {
  lastSql = "";
  lastParameters: unknown[] = [];
  sqlHistory: string[] = [];
  parameterHistory: unknown[][] = [];
  updateChanges = 1;

  prepare(sql: string): D1PreparedStatement {
    return new AdminStatement(this, sql) as unknown as D1PreparedStatement;
  }
}

function environment(database = new AdminD1()) {
  return { FEEDBACK_DB: database as unknown as D1Database, ADMIN_EMAILS: email };
}

function adminRequest(path: string, init: RequestInit = {}, authenticated = true): Request {
  const headers = new Headers(init.headers);
  if (authenticated) headers.set("cf-access-authenticated-user-email", email);
  return new Request(`https://feedback.example.test${path}`, { ...init, headers });
}

describe("feedback administration", () => {
  it("rejects administrative pages without an allowed Access identity", async () => {
    const response = await handleAdminRequest(adminRequest("/admin", {}, false), environment());
    expect(response.status).toBe(401);
    expect(response.headers.get("cache-control")).toBe("no-store");
  });

  it("serves the private triage inbox", async () => {
    const response = await handleAdminRequest(adminRequest("/admin"), environment());
    expect(response.status).toBe(200);
    const page = await response.text();
    expect(page).toContain("Feedback triage");
    expect(page).toContain('href="/admin.css"');
    expect(page).toContain('src="/admin.js"');
    expect(page).toContain('id="save-all"');
    expect(page).toContain('id="delete-selected"');
    expect(page).toContain('href="/admin/audit"');
    expect(response.headers.get("content-security-policy")).toContain("frame-ancestors 'none'");
  });

  it("serves authenticated dashboard assets separately", async () => {
    const css = await handleAdminRequest(adminRequest("/admin.css"), environment());
    const script = await handleAdminRequest(adminRequest("/admin.js"), environment());
    expect(css.headers.get("content-type")).toContain("text/css");
    expect(script.headers.get("content-type")).toContain("text/javascript");
    expect(await script.text()).toContain("async function loadFeedback");
  });

  it("serves admin action receipts on their own page", async () => {
    const response = await handleAdminRequest(adminRequest("/admin/audit"), environment());
    const page = await response.text();
    expect(response.status).toBe(200);
    expect(page).toContain("Admin action receipts");
    expect(page).toContain('src="/audit.js"');
    expect(page).toContain('href="/admin"');
  });

  it("lists filtered feedback and separates diagnostic context", async () => {
    const database = new AdminD1();
    const response = await handleAdminRequest(
      adminRequest("/v1/admin/submissions?q=wrong&status=new&platform=Linux"), environment(database),
    );
    const body = await response.json() as { submissions: Array<Record<string, unknown>> };

    expect(response.status).toBe(200);
    expect(database.lastSql).toContain("status = ?");
    expect(database.lastParameters).toContain("%Environment:%Linux%");
    expect(body.submissions[0]?.userFeedback).not.toContain("Environment:");
    expect(body.submissions[0]?.diagnosticContext).toBe("Practice Takes 0.3.1 | Linux");
    expect(body.submissions[0]).toMatchObject({
      feedbackType: "Bug",
      title: "Wrong note",
      description: "The note is wrong.",
    });
  });

  it("updates workflow metadata and marks duplicate reports", async () => {
    const database = new AdminD1();
    const duplicateOf = "22222222-2222-4222-8222-222222222222";
    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ duplicateOf, developerNotes: "Same root cause", tags: ["tuner"] }),
    }), environment(database));

    expect(response.status).toBe(200);
    expect(database.sqlHistory.some((sql) => sql.includes("duplicate_of = ?"))).toBe(true);
    expect(database.sqlHistory.some((sql) => sql.includes("status = ?"))).toBe(true);
    expect(database.parameterHistory.flat()).toContain("duplicate");
    expect(database.lastSql).toContain("INSERT INTO admin_action_receipts");
  });

  it("creates manual feedback and records the administrator", async () => {
    const database = new AdminD1();
    const response = await handleAdminRequest(adminRequest("/v1/admin/submissions", {
      method: "POST", headers: { "content-type": "application/json" },
      body: JSON.stringify({ category: "idea", appVersion: "0.3.1", message: "Add a metronome." }),
    }), environment(database));
    expect(response.status).toBe(201);
    expect(database.sqlHistory.some((sql) => sql.includes("INSERT INTO feedback_submissions"))).toBe(true);
    expect(database.lastParameters).toContain(email);
    expect(database.lastParameters).toContain("create");
  });

  it("reads one feedback record", async () => {
    const response = await handleAdminRequest(
      adminRequest(`/v1/admin/submissions/${receiptId}`), environment(),
    );
    const body = await response.json() as { submission: { receiptId: string } };
    expect(response.status).toBe(200);
    expect(body.submission.receiptId).toBe(receiptId);
  });

  it("deletes feedback and records the administrator", async () => {
    const database = new AdminD1();
    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "DELETE",
    }), environment(database));
    expect(response.status).toBe(204);
    expect(database.sqlHistory.some((sql) => sql.includes("DELETE FROM feedback_submissions"))).toBe(true);
    expect(database.lastParameters).toContain("delete");
    expect(database.lastParameters.join(" ")).toContain("Wrong note");
  });

  it("batch deletes selected feedback and audits every deletion", async () => {
    const database = new AdminD1();
    const secondReceiptId = "22222222-2222-4222-8222-222222222222";
    const response = await handleAdminRequest(adminRequest("/v1/admin/submissions/batch-delete", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ receiptIds: [receiptId, secondReceiptId] }),
    }), environment(database));
    const body = await response.json() as { deleted: string[] };

    expect(response.status).toBe(200);
    expect(body.deleted).toEqual([receiptId, secondReceiptId]);
    expect(database.sqlHistory.filter((sql) => sql.includes("DELETE FROM feedback_submissions"))).toHaveLength(2);
    expect(database.sqlHistory.filter((sql) => sql.includes("INSERT INTO admin_action_receipts"))).toHaveLength(2);
  });

  it("lists admin action receipts", async () => {
    const response = await handleAdminRequest(adminRequest("/v1/admin/audit"), environment());
    const body = await response.json() as { actions: Array<{ adminEmail: string; action: string }> };
    expect(response.status).toBe(200);
    expect(body.actions[0]).toMatchObject({ adminEmail: email, action: "update" });
  });

  it("reports protected service operations", async () => {
    const response = await handleAdminRequest(
      adminRequest("/v1/admin/operations"), environment(),
    );
    const body = await response.json() as {
      operations: { availabilityPercent: number; quarantinedSubmissions: number };
    };
    expect(response.status).toBe(200);
    expect(body.operations.availabilityPercent).toBe(95);
    expect(body.operations.quarantinedSubmissions).toBe(1);
  });

  it("runs retention on demand and records the maintenance result", async () => {
    const database = new AdminD1();
    const response = await handleAdminRequest(
      adminRequest("/v1/admin/maintenance/retention", { method: "POST" }),
      environment(database),
    );
    expect(response.status).toBe(200);
    expect(database.sqlHistory.some((sql) =>
      sql.includes("DELETE FROM feedback_submissions WHERE status = 'resolved'"))).toBe(true);
    expect(database.sqlHistory.some((sql) =>
      sql.includes("INSERT INTO maintenance_runs"))).toBe(true);
  });

  it("rejects malformed GitHub issue links", async () => {
    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ githubIssueUrl: "https://example.com/issues/1" }),
    }), environment());
    expect(response.status).toBe(400);
  });

  it("exports selected records as CSV", async () => {
    const response = await handleAdminRequest(
      adminRequest(`/v1/admin/export?id=${receiptId}`), environment(),
    );
    expect(response.status).toBe(200);
    expect(response.headers.get("content-type")).toContain("text/csv");
    expect(await response.text()).toContain(receiptId);
  });
});
