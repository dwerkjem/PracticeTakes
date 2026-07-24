import { createServer } from "node:http";
import { timingSafeEqual } from "node:crypto";

import { handleAdminRequest } from "./admin.js";

interface QueryResponse<T> {
  success: boolean;
  errors?: Array<{ message?: string }>;
  result?: Array<{ results?: T[]; meta?: D1Meta; success?: boolean }>;
}

class RemoteStatement {
  private parameters: unknown[] = [];

  constructor(private readonly database: RemoteD1, private readonly sql: string) {}

  bind(...parameters: unknown[]): RemoteStatement {
    this.parameters = parameters;
    return this;
  }

  async all<T>(): Promise<D1Result<T>> {
    const result = await this.database.query<T>(this.sql, this.parameters);
    return { success: true, results: result.results ?? [], meta: result.meta ?? {} } as D1Result<T>;
  }

  async first<T>(): Promise<T | null> {
    const result = await this.database.query<T>(this.sql, this.parameters);
    return result.results?.[0] ?? null;
  }

  async run(): Promise<D1Result> {
    const result = await this.database.query(this.sql, this.parameters);
    return { success: true, results: result.results ?? [], meta: result.meta ?? {} } as D1Result;
  }
}

class RemoteD1 {
  constructor(
    private readonly accountId: string,
    private readonly databaseId: string,
    private readonly token: string,
  ) {}

  prepare(sql: string): D1PreparedStatement {
    return new RemoteStatement(this, sql) as unknown as D1PreparedStatement;
  }

  async query<T>(sql: string, params: unknown[]) {
    const response = await fetch(
      `https://api.cloudflare.com/client/v4/accounts/${encodeURIComponent(this.accountId)}/d1/database/${encodeURIComponent(this.databaseId)}/query`,
      {
        method: "POST",
        headers: { authorization: `Bearer ${this.token}`, "content-type": "application/json" },
        body: JSON.stringify({ sql, params }),
      },
    );
    const body = await response.json() as QueryResponse<T>;
    if (!response.ok || !body.success || !body.result?.[0]?.success) {
      throw new Error(body.errors?.map((error) => error.message).filter(Boolean).join("; ") ||
        `Cloudflare D1 query failed with HTTP ${response.status}`);
    }
    return body.result[0];
  }
}

const required = (name: string): string => {
  const value = process.env[name]?.trim();
  if (!value) throw new Error(`${name} must be set`);
  return value;
};

const adminEmail = required("ADMIN_EMAIL").toLowerCase();
const adminPassword = required("ADMIN_PASSWORD");
const database = new RemoteD1(
  required("CLOUDFLARE_ACCOUNT_ID"), required("D1_DATABASE_ID"), required("CLOUDFLARE_API_TOKEN"),
);
const port = Number.parseInt(process.env.PORT ?? "8787", 10);

function dashboardFailureMessage(error: unknown): string {
  const detail = error instanceof Error ? error.message : "";
  if (/no such (?:table|column):/i.test(detail)) {
    return "The hosted feedback database schema is out of date. " +
      "Run migrate-feedback-database.sh --remote.";
  }
  return "The dashboard could not reach Cloudflare D1.";
}

function authorized(header: string | undefined): boolean {
  if (!header?.startsWith("Basic ")) return false;
  try {
    const decoded = Buffer.from(header.slice(6), "base64").toString("utf8");
    const separator = decoded.indexOf(":");
    const email = decoded.slice(0, separator).toLowerCase();
    const password = decoded.slice(separator + 1);
    const actual = Buffer.from(`${email}\0${password}`);
    const expected = Buffer.from(`${adminEmail}\0${adminPassword}`);
    return actual.length === expected.length && timingSafeEqual(actual, expected);
  } catch { return false; }
}

createServer(async (incoming, outgoing) => {
  try {
    if (!authorized(incoming.headers.authorization)) {
      outgoing.writeHead(401, {
        "cache-control": "no-store",
        "content-type": "text/plain; charset=utf-8",
        "www-authenticate": 'Basic realm="Practice Takes feedback", charset="UTF-8"',
      });
      outgoing.end("Authentication required.");
      return;
    }

    const chunks: Buffer[] = [];
    for await (const chunk of incoming) chunks.push(Buffer.from(chunk));
    const headers = new Headers();
    for (const [name, value] of Object.entries(incoming.headers)) {
      if (value !== undefined) headers.set(name, Array.isArray(value) ? value.join(", ") : value);
    }
    headers.set("cf-access-authenticated-user-email", adminEmail);
    const request = new Request(`http://localhost:${port}${incoming.url ?? "/"}`, {
      method: incoming.method,
      headers,
      body: chunks.length ? Buffer.concat(chunks) : undefined,
    });
    const response = await handleAdminRequest(request, {
      FEEDBACK_DB: database as unknown as D1Database,
      ADMIN_EMAILS: adminEmail,
    });
    outgoing.writeHead(response.status, Object.fromEntries(response.headers));
    outgoing.end(Buffer.from(await response.arrayBuffer()));
  } catch (error) {
    console.error(error);
    outgoing.writeHead(502, { "cache-control": "no-store", "content-type": "text/plain; charset=utf-8" });
    outgoing.end(dashboardFailureMessage(error));
  }
}).listen(port, "0.0.0.0", () => {
  console.log(`Practice Takes feedback dashboard listening on port ${port}`);
});
