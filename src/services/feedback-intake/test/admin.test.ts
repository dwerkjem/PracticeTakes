import { describe, expect, it, vi } from "vitest";

import { handleAdminRequest as handleAdminRequestWithIdentity } from "../src/admin";
import {
  asD1,
  createTestDatabase,
  seedAdminAction,
  seedMaintenanceRun,
  seedQueuedFeedback,
  seedRequestMetric,
  seedSubmission,
  type SubmissionSeed,
  type TestDatabase,
} from "./support/database";

const email = "developer@example.com";
const receiptId = "11111111-1111-4111-8111-111111111111";
const secondReceiptId = "22222222-2222-4222-8222-222222222222";
const structuredMessage =
  "Type: Bug\nTitle: Wrong note\n\nDescription:\nThe note is wrong.\nEnvironment: Practice Takes 0.3.1 | Linux";

interface AdminAction {
  admin_email: string;
  action: string;
  receipt_id: string;
  details_json: string;
}

function environment(database: TestDatabase = createTestDatabase()) {
  return { FEEDBACK_DB: asD1(database), database };
}

function withSubmission(seed: SubmissionSeed = {}) {
  const database = createTestDatabase();
  seedSubmission(database, { receiptId, message: structuredMessage, ...seed });
  return environment(database);
}

function adminActions(database: TestDatabase): AdminAction[] {
  return database.rows<AdminAction>(
    "SELECT admin_email, action, receipt_id, details_json FROM admin_action_receipts ORDER BY id ASC",
  );
}

function adminRequest(path: string, init: RequestInit = {}, authenticated = true): Request {
  const headers = new Headers(init.headers);
  if (authenticated) headers.set("cf-access-authenticated-user-email", email);
  return new Request(`https://feedback.example.test${path}`, { ...init, headers });
}

function handleAdminRequest(
  request: Request,
  env: Parameters<typeof handleAdminRequestWithIdentity>[1],
): Promise<Response> {
  return handleAdminRequestWithIdentity(
    request,
    env,
    request.headers.get("cf-access-authenticated-user-email"),
  );
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
    const database = createTestDatabase();
    seedSubmission(database, { receiptId, message: structuredMessage });
    seedSubmission(database, {
      receiptId: secondReceiptId,
      message: structuredMessage,
      status: "resolved",
    });
    seedSubmission(database, {
      receiptId: "33333333-3333-4333-8333-333333333333",
      message: structuredMessage.replace("| Linux", "| Windows"),
    });
    seedSubmission(database, {
      receiptId: "44444444-4444-4444-8444-444444444444",
      message: "Type: Idea\nTitle: Metronome\n\nDescription:\nAdd one.\nEnvironment: Linux",
    });

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/submissions?q=wrong&status=new&platform=Linux"),
      environment(database),
    );
    const body = await response.json() as { submissions: Array<Record<string, unknown>> };

    expect(response.status).toBe(200);
    // The status, free-text, and platform filters must each exclude a seeded row.
    expect(body.submissions).toHaveLength(1);
    expect(body.submissions[0]?.receiptId).toBe(receiptId);
    expect(body.submissions[0]?.userFeedback).not.toContain("Environment:");
    expect(body.submissions[0]?.diagnosticContext).toBe("Practice Takes 0.3.1 | Linux");
    expect(body.submissions[0]).toMatchObject({
      feedbackType: "Bug",
      title: "Wrong note",
      description: "The note is wrong.",
    });
  });

  it("escapes wildcards in the free-text filter", async () => {
    const database = createTestDatabase();
    seedSubmission(database, { receiptId, developerNotes: "100% reproducible" });
    seedSubmission(database, { receiptId: secondReceiptId, developerNotes: "not reproducible" });

    const response = await handleAdminRequest(
      adminRequest(`/v1/admin/submissions?q=${encodeURIComponent("100%")}`),
      environment(database),
    );
    const body = await response.json() as { submissions: Array<{ receiptId: string }> };

    expect(body.submissions.map((submission) => submission.receiptId)).toEqual([receiptId]);
  });

  it("updates workflow metadata and marks duplicate reports", async () => {
    const database = createTestDatabase();
    seedSubmission(database, { receiptId, message: structuredMessage });
    seedSubmission(database, { receiptId: secondReceiptId, message: structuredMessage });

    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        duplicateOf: secondReceiptId,
        developerNotes: "Same root cause",
        tags: ["tuner"],
      }),
    }), environment(database));

    expect(response.status).toBe(200);
    expect(database.row("SELECT duplicate_of, status, developer_notes, tags_json FROM feedback_submissions WHERE receipt_id = ?", receiptId))
      .toEqual({
        duplicate_of: secondReceiptId,
        status: "duplicate",
        developer_notes: "Same root cause",
        tags_json: '["tuner"]',
      });
    expect(adminActions(database)).toEqual([{
      admin_email: email,
      action: "update",
      receipt_id: receiptId,
      details_json: JSON.stringify({ fields: ["developerNotes", "duplicateOf", "tags"] }),
    }]);
  });

  it("reports a missing record rather than silently updating nothing", async () => {
    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ status: "planned" }),
    }), environment());

    expect(response.status).toBe(404);
  });

  it("creates manual feedback and records the administrator", async () => {
    const env = withSubmission();
    const { database } = env;
    const response = await handleAdminRequest(adminRequest("/v1/admin/submissions", {
      method: "POST", headers: { "content-type": "application/json" },
      body: JSON.stringify({ category: "idea", appVersion: "0.3.1", message: "Add a metronome." }),
    }), env);
    const body = await response.json() as { receiptId: string };

    expect(response.status).toBe(201);
    expect(database.row("SELECT category, app_version, message, installation_hash FROM feedback_submissions WHERE receipt_id = ?", body.receiptId))
      .toEqual({
        category: "idea",
        app_version: "0.3.1",
        message: "Add a metronome.",
        installation_hash: "manual",
      });
    expect(adminActions(database)).toEqual([{
      admin_email: email,
      action: "create",
      receipt_id: body.receiptId,
      details_json: JSON.stringify({ category: "idea", appVersion: "0.3.1", status: "new" }),
    }]);
  });

  it("reads one feedback record", async () => {
    const response = await handleAdminRequest(
      adminRequest(`/v1/admin/submissions/${receiptId}`), withSubmission(),
    );
    const body = await response.json() as { submission: { receiptId: string } };
    expect(response.status).toBe(200);
    expect(body.submission.receiptId).toBe(receiptId);
  });

  it("deletes feedback and records the administrator", async () => {
    const env = withSubmission();
    const { database } = env;
    seedQueuedFeedback(database, receiptId, 1_768_788_000);
    seedSubmission(database, {
      receiptId: secondReceiptId,
      duplicateOf: receiptId,
      status: "duplicate",
    });

    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "DELETE",
    }), env);

    expect(response.status).toBe(204);
    expect(database.count("feedback_submissions", "receipt_id = ?", receiptId)).toBe(0);
    expect(database.count("feedback_email_queue", "receipt_id = ?", receiptId)).toBe(0);
    // Reports marked as duplicates of the deleted record return to triage.
    expect(database.row("SELECT duplicate_of, status FROM feedback_submissions WHERE receipt_id = ?", secondReceiptId))
      .toEqual({ duplicate_of: null, status: "needs_review" });
    expect(adminActions(database)).toEqual([{
      admin_email: email,
      action: "delete",
      receipt_id: receiptId,
      details_json: JSON.stringify({ title: "Wrong note" }),
    }]);
  });

  it("batch deletes selected feedback and audits every deletion", async () => {
    const database = createTestDatabase();
    seedSubmission(database, { receiptId, message: structuredMessage });
    seedSubmission(database, { receiptId: secondReceiptId, message: structuredMessage });
    seedQueuedFeedback(database, receiptId, 1_768_788_000);
    seedQueuedFeedback(database, secondReceiptId, 1_768_788_001);

    const response = await handleAdminRequest(adminRequest("/v1/admin/submissions/batch-delete", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ receiptIds: [receiptId, secondReceiptId] }),
    }), environment(database));
    const body = await response.json() as { deleted: string[]; missing: string[] };

    expect(response.status).toBe(200);
    expect(body.deleted).toEqual([receiptId, secondReceiptId]);
    expect(body.missing).toEqual([]);
    expect(database.count("feedback_submissions")).toBe(0);
    expect(database.count("feedback_email_queue")).toBe(0);
    expect(adminActions(database).map((action) => action.action)).toEqual(["delete", "delete"]);
  });

  it("separates records that were already gone from those it deleted", async () => {
    const env = withSubmission();
    const response = await handleAdminRequest(adminRequest("/v1/admin/submissions/batch-delete", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ receiptIds: [receiptId, secondReceiptId] }),
    }), env);
    const body = await response.json() as { deleted: string[]; missing: string[] };

    expect(body.deleted).toEqual([receiptId]);
    expect(body.missing).toEqual([secondReceiptId]);
    expect(adminActions(env.database)).toHaveLength(1);
  });

  it("lists admin action receipts", async () => {
    const database = createTestDatabase();
    seedAdminAction(database, {
      adminEmail: email,
      action: "update",
      receiptId,
      detailsJson: '{"fields":["status"]}',
    });

    const response = await handleAdminRequest(adminRequest("/v1/admin/audit"), environment(database));
    const body = await response.json() as {
      actions: Array<{ adminEmail: string; action: string; details: unknown }>;
    };
    expect(response.status).toBe(200);
    expect(body.actions[0]).toMatchObject({ adminEmail: email, action: "update" });
    expect(body.actions[0]?.details).toEqual({ fields: ["status"] });
  });

  it("reports protected service operations", async () => {
    const database = createTestDatabase();
    const hour = Math.floor(Date.now() / 1000 / 3600) * 3600;
    seedRequestMetric(database, {
      hour, route: "submissions", outcome: "success", requestCount: 19,
      totalDurationMs: 950, maximumDurationMs: 150,
    });
    seedRequestMetric(database, {
      hour, route: "submissions", outcome: "failure", requestCount: 1,
      totalDurationMs: 50, maximumDurationMs: 50,
    });
    seedSubmission(database, { receiptId });
    seedSubmission(database, {
      receiptId: secondReceiptId,
      quarantineReason: "multiple_external_links",
      quarantinedAt: 1_768_788_000,
      status: "needs_review",
    });
    seedMaintenanceRun(database, {
      actor: email, completedAt: 1_768_788_000, detailsJson: '{"resolved":1}',
    });

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/operations"), environment(database),
    );
    const body = await response.json() as {
      operations: {
        availabilityPercent: number; quarantinedSubmissions: number;
        requests: number; failures: number; maximumResponseMs: number;
        storedSubmissions: number; lastRetentionResult: unknown;
      };
    };

    expect(response.status).toBe(200);
    expect(body.operations.availabilityPercent).toBe(95);
    expect(body.operations.quarantinedSubmissions).toBe(1);
    expect(body.operations.requests).toBe(20);
    expect(body.operations.failures).toBe(1);
    expect(body.operations.maximumResponseMs).toBe(150);
    expect(body.operations.storedSubmissions).toBe(2);
    expect(body.operations.lastRetentionResult).toEqual({ resolved: 1 });
  });

  it("runs retention on demand and records the maintenance result", async () => {
    const database = createTestDatabase();
    const now = Math.floor(Date.now() / 1000);
    const longAgo = now - 400 * 24 * 60 * 60;
    seedSubmission(database, { receiptId, status: "resolved", receivedAt: longAgo });
    seedSubmission(database, {
      receiptId: secondReceiptId, status: "new", receivedAt: now,
    });
    seedQueuedFeedback(database, receiptId, longAgo);
    database.execute(
      "INSERT INTO authorization_requests (client_hash, requested_at) VALUES (?, ?)",
      "expired-client",
      longAgo,
    );

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/maintenance/retention", { method: "POST" }),
      environment(database),
    );
    const body = await response.json() as {
      retention: { resolved: number; emailQueue: number; authorizationRequests: number };
    };

    expect(response.status).toBe(200);
    expect(body.retention.resolved).toBe(1);
    expect(body.retention.emailQueue).toBe(1);
    expect(body.retention.authorizationRequests).toBe(1);
    expect(database.rows("SELECT receipt_id FROM feedback_submissions"))
      .toEqual([{ receipt_id: secondReceiptId }]);
    expect(database.count("feedback_email_queue")).toBe(0);
    expect(database.count("authorization_requests")).toBe(0);
    expect(database.row<{ actor: string; details_json: string }>(
      "SELECT actor, details_json FROM maintenance_runs WHERE operation = 'retention'",
    )?.actor).toBe(email);
  });

  it("rejects malformed GitHub issue links", async () => {
    const response = await handleAdminRequest(adminRequest(`/v1/admin/submissions/${receiptId}`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ githubIssueUrl: "https://example.com/issues/1" }),
    }), withSubmission());
    expect(response.status).toBe(400);
  });

  it("exports selected records as CSV", async () => {
    const response = await handleAdminRequest(
      adminRequest(`/v1/admin/export?id=${receiptId}`), withSubmission(),
    );
    expect(response.status).toBe(200);
    expect(response.headers.get("content-type")).toContain("text/csv");
    expect(await response.text()).toContain(receiptId);
  });

  it("neutralizes spreadsheet formulas in exported cells", async () => {
    const env = withSubmission({
      message: '=HYPERLINK("https://attacker.test","Click")',
      developerNotes: "-2+3+cmd|' /c calc'!A0",
    });
    const response = await handleAdminRequest(
      adminRequest(`/v1/admin/export?id=${receiptId}`), env,
    );
    const csv = await response.text();

    expect(csv).toContain(`"'=HYPERLINK(""https://attacker.test"",""Click"")"`);
    expect(csv).toContain(`"'-2+3+cmd|' /c calc'!A0"`);
  });

  it("rejects administrative writes that omit the JSON content type", async () => {
    const response = await handleAdminRequest(adminRequest("/v1/admin/submissions/batch-delete", {
      method: "POST",
      body: JSON.stringify({ receiptIds: [receiptId] }),
    }), environment());
    const body = await response.json() as { error: { code: string } };

    expect(response.status).toBe(415);
    expect(body.error.code).toBe("unsupported_media_type");
  });
});

describe("the notification administration routes", () => {
  function notificationEnvironment(
    database: TestDatabase,
    overrides: Record<string, unknown> = {},
  ) {
    const send = vi.fn(async (_message: unknown) => ({ messageId: "cf-admin-1" }));
    return {
      env: {
        FEEDBACK_DB: asD1(database),
        FEEDBACK_EMAIL: { send: send as unknown as SendEmail["send"] },
        FEEDBACK_NOTIFICATION_FROM: "feedback@practicetakes.app",
        FEEDBACK_NOTIFICATION_TO: email,
        FEEDBACK_DASHBOARD_URL: "https://feedback.example.test/admin",
        ADMIN_EMAILS: email,
        ...overrides,
      },
      send,
      database,
    };
  }

  it("reports delivery status without sending, claiming, or reserving", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, receiptId, 1_756_000_000);
    const { env, send } = notificationEnvironment(database);

    const response = await handleAdminRequest(adminRequest("/v1/admin/notifications"), env);
    const body = await response.json() as {
      notifications: {
        configured: boolean;
        problems: string[];
        queue: { pending: number; oldestReceivedAt: string | null };
        daily: { limit: number; remaining: number };
        lastAttempt: unknown;
      };
    };

    expect(response.status).toBe(200);
    expect(body.notifications.configured).toBe(true);
    expect(body.notifications.problems).toEqual([]);
    expect(body.notifications.queue.pending).toBe(1);
    expect(body.notifications.daily).toMatchObject({ limit: 3, remaining: 3 });
    expect(body.notifications.lastAttempt).toBeNull();
    expect(send).not.toHaveBeenCalled();
    expect(database.count("maintenance_runs")).toBe(0);
  });

  it("names the configuration problem when delivery is broken", async () => {
    const { env } = notificationEnvironment(createTestDatabase(), {
      FEEDBACK_NOTIFICATION_FROM: "feedback@example.com",
    });

    const response = await handleAdminRequest(adminRequest("/v1/admin/notifications"), env);
    const body = await response.json() as {
      notifications: { configured: boolean; problems: string[] };
    };

    expect(response.status).toBe(200);
    expect(body.notifications.configured).toBe(false);
    expect(body.notifications.problems).toEqual(["sender_domain_not_sendable"]);
  });

  it("dispatches queued feedback on demand under the caller's identity", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, receiptId, 1_756_000_000);
    const { env, send } = notificationEnvironment(database);

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch", { method: "POST" }), env,
    );
    const body = await response.json() as {
      dispatch: { outcome: string; sent: number; messageId: string | null };
    };

    expect(response.status).toBe(200);
    expect(body.dispatch).toMatchObject({
      outcome: "sent", sent: 1, messageId: "cf-admin-1",
    });
    expect(send).toHaveBeenCalledOnce();
    expect(database.count("feedback_email_queue")).toBe(0);
    expect(database.row<{ actor: string }>(
      "SELECT actor FROM maintenance_runs WHERE operation = 'notification'",
    )?.actor).toBe(email);
  });

  it("reports an empty queue as a successful run that sent nothing", async () => {
    const { env, send } = notificationEnvironment(createTestDatabase());

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch", { method: "POST" }), env,
    );
    const body = await response.json() as { dispatch: { outcome: string } };

    expect(response.status).toBe(200);
    expect(body.dispatch.outcome).toBe("nothing_pending");
    expect(send).not.toHaveBeenCalled();
  });

  it("refuses with 503 when delivery is not configured, leaving the queue intact", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, receiptId, 1_756_000_000);
    const { env, send } = notificationEnvironment(database, {
      FEEDBACK_NOTIFICATION_FROM: "feedback@example.com",
    });

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch", { method: "POST" }), env,
    );
    const body = await response.json() as {
      dispatch: { outcome: string; problems: string[] };
      error: { code: string; message: string };
    };

    expect(response.status).toBe(503);
    expect(body.error.code).toBe("notifications_not_configured");
    expect(body.error.message).toContain("sender_domain_not_sendable");
    expect(body.dispatch.problems).toEqual(["sender_domain_not_sendable"]);
    expect(send).not.toHaveBeenCalled();
    expect(database.count("feedback_email_queue")).toBe(1);
  });

  it("reports a failed send as 503 and returns the batch to the queue", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, receiptId, 1_756_000_000);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const { env, send } = notificationEnvironment(database);
    send.mockRejectedValueOnce(new Error("mail service unavailable"));

    const response = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch", { method: "POST" }), env,
    );
    const body = await response.json() as { error: { code: string; message: string } };

    expect(response.status).toBe(503);
    expect(body.error.code).toBe("notification_send_failed");
    expect(body.error.message).toBe("mail service unavailable");
    expect(database.count("feedback_email_queue")).toBe(1);
    errorLog.mockRestore();
  });

  it("honours the daily limit that the scheduled dispatch honours", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, receiptId, 1_756_000_000);
    seedQueuedFeedback(database, secondReceiptId, 1_756_000_100);
    const { env, send } = notificationEnvironment(database, {
      FEEDBACK_MAX_DAILY_EMAILS: "1",
    });

    const first = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch", { method: "POST" }), env,
    );
    seedQueuedFeedback(database, "33333333-3333-4333-8333-333333333333", 1_756_000_200);
    const second = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch", { method: "POST" }), env,
    );
    const body = await second.json() as { dispatch: { outcome: string; dailyLimit: number } };

    expect(first.status).toBe(200);
    expect(second.status).toBe(200);
    expect(body.dispatch).toMatchObject({ outcome: "daily_limit_reached", dailyLimit: 1 });
    expect(send).toHaveBeenCalledOnce();
  });

  it("refuses both routes without an authorized administrator identity", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, receiptId, 1_756_000_000);
    const { env, send } = notificationEnvironment(database);

    for (const [path, method] of [
      ["/v1/admin/notifications", "GET"],
      ["/v1/admin/notifications/dispatch", "POST"],
    ] as const) {
      const response = await handleAdminRequest(
        adminRequest(path, { method }, false), env,
      );
      expect(response.status).toBe(401);
      expect(await response.text()).not.toContain("practicetakes.app");
    }
    expect(send).not.toHaveBeenCalled();
    expect(database.count("feedback_email_queue")).toBe(1);
  });

  it("does not dispatch on GET or report status on POST", async () => {
    const { env, send } = notificationEnvironment(createTestDatabase());

    const dispatched = await handleAdminRequest(
      adminRequest("/v1/admin/notifications", { method: "POST" }), env,
    );
    const status = await handleAdminRequest(
      adminRequest("/v1/admin/notifications/dispatch"), env,
    );

    expect(dispatched.status).toBe(404);
    expect(status.status).toBe(404);
    expect(send).not.toHaveBeenCalled();
  });
});
