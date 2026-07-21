import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import worker from "../src/index";

const installationId = "test-installation-identifier-0001";
const signingKey = "test-only-signing-key-with-at-least-32-characters";
const clientAddress = "192.0.2.10";

interface StoredAuthorizationRequest {
  clientHash: string;
  requestedAt: number;
}

interface StoredSubmission {
  receiptId: string;
  submittedAt: string;
  receivedAt: number;
  appVersion: string;
  installationHash: string;
  clientHash: string;
  category: string;
  message: string;
  contactEmail: string | null;
}

class MemoryStatement {
  constructor(
    readonly database: MemoryD1,
    readonly sql: string,
    readonly parameters: unknown[] = [],
  ) {}

  bind(...parameters: unknown[]): MemoryStatement {
    return new MemoryStatement(this.database, this.sql, parameters);
  }

  async first<T>(): Promise<T | null> {
    const [value, cutoff] = this.parameters as [string, number];
    let count = 0;

    if (this.sql.includes("FROM authorization_requests")) {
      count = this.database.authorizationRequests.filter(
        (request) => request.clientHash === value && request.requestedAt >= cutoff,
      ).length;
    } else if (this.sql.includes("installation_hash")) {
      count = this.database.submissions.filter(
        (submission) => submission.installationHash === value && submission.receivedAt >= cutoff,
      ).length;
    } else if (this.sql.includes("client_hash")) {
      count = this.database.submissions.filter(
        (submission) => submission.clientHash === value && submission.receivedAt >= cutoff,
      ).length;
    }

    return { count } as T;
  }

  async run(): Promise<D1Result> {
    this.database.execute(this);
    return { success: true, meta: {} } as D1Result;
  }
}

class MemoryD1 {
  readonly authorizationRequests: StoredAuthorizationRequest[] = [];
  readonly consumedTokenIds = new Set<string>();
  readonly submissions: StoredSubmission[] = [];

  prepare(sql: string): D1PreparedStatement {
    return new MemoryStatement(this, sql) as unknown as D1PreparedStatement;
  }

  async batch(statements: D1PreparedStatement[]): Promise<D1Result[]> {
    const memoryStatements = statements as unknown as MemoryStatement[];
    const consumedStatement = memoryStatements.find((statement) =>
      statement.sql.includes("INSERT INTO consumed_authorizations"),
    );
    const tokenId = consumedStatement?.parameters[0] as string | undefined;
    if (tokenId && this.consumedTokenIds.has(tokenId)) {
      throw new Error("UNIQUE constraint failed: consumed_authorizations.token_id");
    }

    for (const statement of memoryStatements) this.execute(statement);
    return memoryStatements.map(() => ({ success: true, meta: {} }) as D1Result);
  }

  execute(statement: MemoryStatement): void {
    if (statement.sql.includes("INSERT INTO authorization_requests")) {
      const [clientHash, requestedAt] = statement.parameters as [string, number];
      this.authorizationRequests.push({ clientHash, requestedAt });
      return;
    }

    if (statement.sql.includes("INSERT INTO consumed_authorizations")) {
      this.consumedTokenIds.add(statement.parameters[0] as string);
      return;
    }

    if (statement.sql.includes("INSERT INTO feedback_submissions")) {
      const [receiptId, submittedAt, receivedAt, appVersion, installationHash, clientHash,
        category, message, contactEmail] = statement.parameters as
        [string, string, number, string, string, string, string, string, string | null];
      this.submissions.push({ receiptId, submittedAt, receivedAt, appVersion, installationHash,
                              clientHash, category, message, contactEmail });
    }
  }
}

function createEnvironment(database = new MemoryD1(), overrides: Record<string, string> = {}) {
  return {
    FEEDBACK_DB: database as unknown as D1Database,
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

async function send(path: string, body: unknown, environment: ReturnType<typeof createEnvironment>,
                    method = "POST"): Promise<Response> {
  return worker.fetch(request(path, body, method), environment as never);
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
    category: "bug",
    message: "The tuner displayed an incorrect octave.",
  };
}

async function errorCode(response: Response): Promise<string> {
  const body = await response.json() as { error: { code: string } };
  return body.error.code;
}

describe("feedback intake worker", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-07-19T02:00:00.000Z"));
  });

  afterEach(() => vi.useRealTimers());

  it("accepts a valid report and returns a receipt", async () => {
    const database = new MemoryD1();
    const environment = createEnvironment(database);
    const authorization = await authorize(environment);

    const response = await send("/v1/submissions", feedback(authorization), environment);
    const body = await response.json() as { schemaVersion: number; receiptId: string };

    expect(response.status).toBe(201);
    expect(body.schemaVersion).toBe(1);
    expect(body.receiptId).toMatch(/^[0-9a-f-]{36}$/);
    expect(database.submissions).toHaveLength(1);
    expect(database.submissions[0]?.message).toBe("The tuner displayed an incorrect octave.");
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

  it("rejects oversized requests before authorization processing", async () => {
    const response = await send("/v1/submissions", {
      ...feedback("not-a-real-token"),
      message: "x".repeat(1_600_000),
    }, createEnvironment());

    expect(response.status).toBe(413);
    expect(await errorCode(response)).toBe("payload_too_large");
  });

  it("enforces the submission rate limit", async () => {
    const environment = createEnvironment(new MemoryD1(), { SUBMISSIONS_PER_HOUR: "1" });
    const firstAuthorization = await authorize(environment);
    expect((await send("/v1/submissions", feedback(firstAuthorization), environment)).status).toBe(201);

    const secondAuthorization = await authorize(environment);
    const response = await send("/v1/submissions", feedback(secondAuthorization), environment);

    expect(response.status).toBe(429);
    expect(await errorCode(response)).toBe("rate_limited");
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

    const response = await worker.fetch(insecureRequest, createEnvironment() as never);
    expect(response.status).toBe(400);
    expect(await errorCode(response)).toBe("https_required");
  });
});
