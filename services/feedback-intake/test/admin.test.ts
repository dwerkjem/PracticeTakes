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
};

class AdminStatement {
  parameters: unknown[] = [];

  constructor(readonly database: AdminD1, readonly sql: string) {}

  bind(...parameters: unknown[]): AdminStatement {
    this.parameters = parameters;
    this.database.lastParameters = parameters;
    return this;
  }

  async all<T>(): Promise<D1Result<T>> {
    this.database.lastSql = this.sql;
    return { success: true, results: [row] as T[], meta: {} } as D1Result<T>;
  }

  async run(): Promise<D1Result> {
    this.database.lastSql = this.sql;
    return { success: true, meta: { changes: this.database.updateChanges } } as D1Result;
  }
}

class AdminD1 {
  lastSql = "";
  lastParameters: unknown[] = [];
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
    expect(await response.text()).toContain("Feedback triage");
    expect(response.headers.get("content-security-policy")).toContain("frame-ancestors 'none'");
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
    expect(database.lastSql).toContain("duplicate_of = ?");
    expect(database.lastSql).toContain("status = ?");
    expect(database.lastParameters).toContain("duplicate");
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
