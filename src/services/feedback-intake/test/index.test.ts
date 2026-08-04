import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import worker from "../src/index";
import { asD1, createTestDatabase, type TestDatabase } from "./support/database";

const installationId = "test-installation-identifier-0001";
const signingKey = "test-only-signing-key-with-at-least-32-characters";
const clientAddress = "192.0.2.10";

interface StoredSubmission {
  receipt_id: string;
  message: string;
  client_hash: string;
  installation_hash: string;
  status: string;
  quarantine_reason: string | null;
  client_submission_id: string | null;
}

function submissions(database: TestDatabase): StoredSubmission[] {
  return database.rows<StoredSubmission>(
    `SELECT receipt_id, message, client_hash, installation_hash, status,
            quarantine_reason, client_submission_id
       FROM feedback_submissions ORDER BY received_at ASC, rowid ASC`,
  );
}

function createEnvironment(
  database = createTestDatabase(),
  overrides: Record<string, unknown> = {},
) {
  return {
    FEEDBACK_DB: asD1(database),
    SUBMISSION_SIGNING_KEY: signingKey,
    MINIMUM_APP_VERSION: "0.2.6",
    AUTHORIZATIONS_PER_HOUR: "10",
    SUBMISSIONS_PER_HOUR: "5",
    ADMIN_EMAILS: "developer@example.com",
    ...overrides,
  };
}

function request(path: string, body: unknown, method = "POST"): Request {
  return new Request(`https://feedback.example.test${path}`, {
    method,
    headers: {
      "content-type": "application/json",
      "cf-connecting-ip": clientAddress,
    },
    body: method === "POST" ? JSON.stringify(body) : undefined,
  });
}

function executionContext(): {
  context: ExecutionContext;
  completed: () => Promise<void>;
} {
  const pending: Promise<unknown>[] = [];
  return {
    context: {
      passThroughOnException() {},
      waitUntil(promise: Promise<unknown>) {
        pending.push(promise);
      },
    } as ExecutionContext,
    async completed() {
      await Promise.all(pending);
    },
  };
}

async function fetchWorker(
  workerRequest: Request,
  environment: ReturnType<typeof createEnvironment>,
): Promise<Response> {
  const background = executionContext();
  const response = await worker.fetch(
    workerRequest,
    environment as never,
  );
  await background.completed();
  return response;
}

async function send(path: string, body: unknown, environment: ReturnType<typeof createEnvironment>,
                    method = "POST"): Promise<Response> {
  return fetchWorker(request(path, body, method), environment);
}

async function authorize(environment: ReturnType<typeof createEnvironment>): Promise<string> {
  const response = await send("/v1/authorizations", {
    schemaVersion: 1,
    appVersion: "0.2.6",
    installationId,
  }, environment);
  expect(response.status).toBe(201);
  const body = await response.json() as { authorization: string };
  return body.authorization;
}

function feedback(authorization: string) {
  return {
    schemaVersion: 1,
    authorization,
    submittedAt: new Date().toISOString(),
    appVersion: "0.2.6",
    installationId,
    clientSubmissionId: crypto.randomUUID(),
    category: "bug",
    message: "The tuner displayed an incorrect octave.",
  };
}

async function errorCode(response: Response): Promise<string> {
  const body = await response.json() as { error: { code: string } };
  return body.error.code;
}

function attachment(signature: number[]): string {
  const bytes = [...signature, ...Array.from({ length: 32 - signature.length }, () => 0)];
  return btoa(String.fromCharCode(...bytes));
}

const pngSignature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
const jpegSignature = [0xff, 0xd8, 0xff, 0xe0];

describe("feedback intake worker", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-07-19T02:00:00.000Z"));
  });

  afterEach(() => vi.useRealTimers());

  it("keeps hosted administration disabled and rejects spoofed Access identity headers",
     async () => {
       const headers = new Headers({
         "cf-access-authenticated-user-email": "developer@example.com",
       });
       const disabled = await fetchWorker(
         new Request("https://feedback.example.test/admin", { headers }),
         createEnvironment(),
       );
       const enabled = await fetchWorker(
         new Request("https://feedback.example.test/admin", { headers }),
         createEnvironment(undefined, { ADMIN_ROUTES_ENABLED: "true" }),
       );

       expect(disabled.status).toBe(404);
       expect(enabled.status).toBe(401);
     });

  it("accepts a valid report and returns a receipt", async () => {
    const database = createTestDatabase();
    const environment = createEnvironment(database);
    const authorization = await authorize(environment);

    const response = await send("/v1/submissions", feedback(authorization), environment);
    const body = await response.json() as { schemaVersion: number; receiptId: string };

    expect(response.status).toBe(201);
    expect(body.schemaVersion).toBe(1);
    expect(body.receiptId).toMatch(/^[0-9a-f-]{36}$/);
    expect(submissions(database)).toHaveLength(1);
    expect(submissions(database)[0]?.message).toBe("The tuner displayed an incorrect octave.");
  });

  it("stores every report without depending on email delivery", async () => {
    const database = createTestDatabase();
    const sendEmail = vi.fn(async () => ({ messageId: crypto.randomUUID() }));
    const environment = createEnvironment(database, {
      FEEDBACK_EMAIL: { send: sendEmail },
      FEEDBACK_NOTIFICATION_FROM: "feedback@practicetakes.app",
      FEEDBACK_NOTIFICATION_TO: "developer@example.com",
      FEEDBACK_DASHBOARD_URL: "https://feedback.example.test/admin",
    });

    for (let index = 0; index < 5; index += 1) {
      const authorization = await authorize(environment);
      const response = await send(
        "/v1/submissions",
        feedback(authorization),
        environment,
      );
      expect(response.status).toBe(201);
    }

    expect(submissions(database)).toHaveLength(5);
    expect(database.count("feedback_email_queue")).toBe(5);
    expect(sendEmail).not.toHaveBeenCalled();
  });

  it("rejects unsupported application versions", async () => {
    const response = await send("/v1/authorizations", {
      schemaVersion: 1,
      appVersion: "0.2.5",
      installationId,
    }, createEnvironment());

    expect(response.status).toBe(400);
    expect(await errorCode(response)).toBe("unsupported_app_version");
  });

  it("rejects expired authorizations", async () => {
    const environment = createEnvironment();
    const authorization = await authorize(environment);
    vi.advanceTimersByTime(301_000);

    const response = await send("/v1/submissions", feedback(authorization), environment);

    expect(response.status).toBe(401);
    expect(await errorCode(response)).toBe("expired_authorization");
  });

  it("rejects replayed authorizations", async () => {
    const environment = createEnvironment();
    const authorization = await authorize(environment);

    expect((await send("/v1/submissions", feedback(authorization), environment)).status).toBe(201);
    const replay = await send("/v1/submissions", feedback(authorization), environment);

    expect(replay.status).toBe(409);
    expect(await errorCode(replay)).toBe("duplicate_submission");
  });

  it("returns the original receipt when a client retries the same submission", async () => {
    const database = createTestDatabase();
    const environment = createEnvironment(database);
    const report = feedback(await authorize(environment));

    const first = await send("/v1/submissions", report, environment);
    const original = await first.json() as { receiptId: string };
    const retry = await send(
      "/v1/submissions",
      { ...report, authorization: await authorize(environment) },
      environment,
    );
    const body = await retry.json() as { receiptId: string; duplicate: boolean };

    expect(first.status).toBe(201);
    // The partial unique index from 0005_idempotent_submissions.sql is what
    // rejects the retry; the worker turns that conflict into the first receipt.
    expect(retry.status).toBe(200);
    expect(body).toEqual({ schemaVersion: 1, receiptId: original.receiptId, duplicate: true });
    expect(submissions(database)).toHaveLength(1);
  });

  it("leaves no partial rows behind when a submission batch fails", async () => {
    const database = createTestDatabase();
    const environment = createEnvironment(database);
    const report = feedback(await authorize(environment));
    expect((await send("/v1/submissions", report, environment)).status).toBe(201);

    // The retry consumes a fresh authorization, so the batch's first statement
    // succeeds and only the submission insert conflicts.
    const retry = await send(
      "/v1/submissions",
      { ...report, authorization: await authorize(environment) },
      environment,
    );

    expect(retry.status).toBe(200);
    expect(database.count("consumed_authorizations")).toBe(1);
    expect(database.count("feedback_email_queue")).toBe(1);
    expect(submissions(database)).toHaveLength(1);
  });

  it("rejects oversized requests before authorization processing", async () => {
    const response = await send("/v1/submissions", {
      ...feedback("not-a-real-token"),
      message: "x".repeat(1_600_000),
    }, createEnvironment());

    expect(response.status).toBe(413);
    expect(await errorCode(response)).toBe("payload_too_large");
  });

  it("enforces the submission rate limit", async () => {
    const environment = createEnvironment(createTestDatabase(), { SUBMISSIONS_PER_HOUR: "1" });
    const firstAuthorization = await authorize(environment);
    expect((await send("/v1/submissions", feedback(firstAuthorization), environment)).status).toBe(201);

    const secondAuthorization = await authorize(environment);
    const response = await send("/v1/submissions", feedback(secondAuthorization), environment);

    expect(response.status).toBe(429);
    expect(await errorCode(response)).toBe("rate_limited");
  });

  it("caps authorization traffic across all clients", async () => {
    const environment = createEnvironment(createTestDatabase(), {
      MAX_AUTHORIZATIONS_PER_HOUR: "1",
    });
    expect((await send("/v1/authorizations", {
      schemaVersion: 1, appVersion: "0.2.6", installationId,
    }, environment)).status).toBe(201);

    const response = await send("/v1/authorizations", {
      schemaVersion: 1, appVersion: "0.2.6", installationId,
    }, environment);
    expect(response.status).toBe(429);
    expect(await errorCode(response)).toBe("service_rate_limited");
  });

  it("enforces the global storage ceiling before consuming another report", async () => {
    const environment = createEnvironment(createTestDatabase(), {
      MAX_STORED_SUBMISSIONS: "1",
      SUBMISSIONS_PER_HOUR: "5",
    });
    const firstAuthorization = await authorize(environment);
    expect((await send("/v1/submissions", feedback(firstAuthorization), environment)).status).toBe(201);

    const secondAuthorization = await authorize(environment);
    const response = await send("/v1/submissions", feedback(secondAuthorization), environment);

    expect(response.status).toBe(503);
    expect(await errorCode(response)).toBe("storage_capacity_reached");
  });

  it("quarantines suspicious link-heavy reports for manual review", async () => {
    const database = createTestDatabase();
    const environment = createEnvironment(database);
    const authorization = await authorize(environment);
    const report = feedback(authorization);
    report.message = "Review https://one.test https://two.test and https://three.test";

    expect((await send("/v1/submissions", report, environment)).status).toBe(201);
    expect(submissions(database)[0]).toMatchObject({
      status: "needs_review",
      quarantine_reason: "multiple_external_links",
    });
    expect(database.count("feedback_email_queue")).toBe(0);
  });

  it("stores the client address as a keyed pseudonym rather than a plain digest", async () => {
    const database = createTestDatabase();
    const environment = createEnvironment(database);
    const authorization = await authorize(environment);
    expect((await send("/v1/submissions", feedback(authorization), environment)).status).toBe(201);

    const digest = await crypto.subtle.digest(
      "SHA-256", new TextEncoder().encode(clientAddress),
    );
    const unkeyed = btoa(String.fromCharCode(...new Uint8Array(digest)))
      .replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
    expect(submissions(database)[0]?.client_hash).not.toBe(unkeyed);
    expect(submissions(database)[0]?.client_hash).not.toContain(clientAddress);
  });

  it.each([
    ["image/png", pngSignature],
    ["image/jpeg", jpegSignature],
  ] as const)("accepts a %s screenshot whose bytes match the declared image type",
              async (screenshotMimeType, signature) => {
                const environment = createEnvironment();
                const authorization = await authorize(environment);

                const response = await send("/v1/submissions", {
                  ...feedback(authorization),
                  screenshotMimeType,
                  screenshotBase64: attachment([...signature]),
                }, environment);

                expect(response.status).toBe(201);
              });

  it("rejects attachments whose bytes do not match the declared image type", async () => {
    const environment = createEnvironment();
    const authorization = await authorize(environment);

    const response = await send("/v1/submissions", {
      ...feedback(authorization),
      screenshotMimeType: "image/png",
      screenshotBase64: attachment(jpegSignature),
    }, environment);

    expect(response.status).toBe(400);
    expect(await errorCode(response)).toBe("invalid_screenshot");
  });

  it("rejects attachments that are not images at all", async () => {
    const environment = createEnvironment();
    const authorization = await authorize(environment);

    const response = await send("/v1/submissions", {
      ...feedback(authorization),
      screenshotMimeType: "image/png",
      screenshotBase64: attachment([0x4d, 0x5a, 0x90, 0x00]),
    }, environment);

    expect(response.status).toBe(400);
    expect(await errorCode(response)).toBe("invalid_screenshot");
  });

  it("rejects requests carrying fields outside the published contract", async () => {
    const environment = createEnvironment();
    const authorizationResponse = await send("/v1/authorizations", {
      schemaVersion: 1, appVersion: "0.2.6", installationId, debugMode: true,
    }, environment);
    expect(authorizationResponse.status).toBe(400);
    expect(await errorCode(authorizationResponse)).toBe("invalid_request");

    const authorization = await authorize(environment);
    const submissionResponse = await send("/v1/submissions", {
      ...feedback(authorization), internalPriority: "critical",
    }, environment);
    expect(submissionResponse.status).toBe(400);
    expect(await errorCode(submissionResponse)).toBe("invalid_request");
  });

  it("records a telemetry bucket for every public request", async () => {
    const database = createTestDatabase();
    const environment = createEnvironment(database);
    await authorize(environment);

    expect(database.rows<{ route: string; outcome: string; request_count: number }>(
      "SELECT route, outcome, request_count FROM request_metrics",
    )).toEqual([{ route: "authorizations", outcome: "success", request_count: 1 }]);
  });

  it("provides a data-free availability probe", async () => {
    const response = await send("/v1/health", {}, createEnvironment(), "GET");
    expect(response.status).toBe(200);
    expect(await response.json()).toEqual({ status: "ok" });
  });

  it("exposes no read route", async () => {
    const response = await send("/v1/submissions", {}, createEnvironment(), "GET");
    expect(response.status).toBe(405);
    expect(await errorCode(response)).toBe("method_not_allowed");
  });

  it("rejects non-HTTPS requests", async () => {
    const insecureRequest = new Request("http://feedback.example.test/v1/authorizations", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ schemaVersion: 1, appVersion: "0.2.6", installationId }),
    });

    const response = await fetchWorker(insecureRequest, createEnvironment());
    expect(response.status).toBe(400);
    expect(await errorCode(response)).toBe("https_required");
  });
});
