import { handleAdminRequest } from "./admin";

interface Env {
  FEEDBACK_DB: D1Database;
  SUBMISSION_SIGNING_KEY: string;
  MINIMUM_APP_VERSION: string;
  AUTHORIZATIONS_PER_HOUR: string;
  SUBMISSIONS_PER_HOUR: string;
  ADMIN_EMAILS: string;
}

interface AuthorizationRequest {
  schemaVersion: 1;
  appVersion: string;
  installationId: string;
}

interface FeedbackRequest extends AuthorizationRequest {
  authorization: string;
  submittedAt: string;
  category: "bug" | "idea" | "usability" | "other";
  message: string;
  contactEmail?: string;
  screenshotMimeType?: "image/png" | "image/jpeg";
  screenshotBase64?: string;
  clientSubmissionId: string;
}

interface TokenClaims {
  version: 1;
  tokenId: string;
  expiresAt: number;
  appVersion: string;
  installationHash: string;
  clientHash: string;
}

const jsonHeaders = { "content-type": "application/json; charset=utf-8" };
const maximumBodyBytes = 1536 * 1024;
const maximumScreenshotBase64Length = 1400 * 1024;
const authorizationLifetimeSeconds = 5 * 60;
const oneHourSeconds = 60 * 60;
const versionPattern = /^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/;
const installationIdPattern = /^[0-9A-Za-z_-]{20,128}$/;
const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const allowedCategories = new Set(["bug", "idea", "usability", "other"]);

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      if (new URL(request.url).protocol !== "https:") {
        return errorResponse(400, "https_required", "Feedback must be submitted over HTTPS.");
      }

      const pathname = new URL(request.url).pathname;
      if (pathname === "/admin" || pathname.startsWith("/admin/") || pathname === "/admin.css" ||
          pathname === "/admin.js" || pathname === "/audit.js" ||
          pathname.startsWith("/v1/admin/")) {
        return await handleAdminRequest(request, env);
      }

      if (request.method !== "POST") {
        return errorResponse(405, "method_not_allowed", "Only POST is supported.", {
          allow: "POST",
        });
      }

      if (pathname === "/v1/authorizations") {
        return await createAuthorization(request, env);
      }
      if (pathname === "/v1/submissions") {
        return await createSubmission(request, env);
      }

      return errorResponse(404, "not_found", "Route not found.");
    } catch (error) {
      console.error("Unhandled feedback intake error", error);
      return errorResponse(500, "internal_error", "The feedback service could not process the request.");
    }
  },
} satisfies ExportedHandler<Env>;

async function createAuthorization(request: Request, env: Env): Promise<Response> {
  const parsed = await readJson(request);
  if (parsed instanceof Response) return parsed;

  const validation = validateAuthorizationRequest(parsed, env.MINIMUM_APP_VERSION);
  if (validation.error) return validation.error;
  const input = validation.value;

  const now = Math.floor(Date.now() / 1000);
  const clientHash = await hashClient(request);
  const authorizationLimit = positiveInteger(env.AUTHORIZATIONS_PER_HOUR, 10);
  if (await isRateLimited(env.FEEDBACK_DB, "authorization_requests", "client_hash", clientHash,
                          now, authorizationLimit)) {
    return errorResponse(429, "rate_limited", "Too many authorization requests. Try again later.", {
      "retry-after": "3600",
    });
  }

  await env.FEEDBACK_DB.prepare(
    "INSERT INTO authorization_requests (client_hash, requested_at) VALUES (?, ?)",
  ).bind(clientHash, now).run();

  const claims: TokenClaims = {
    version: 1,
    tokenId: crypto.randomUUID(),
    expiresAt: now + authorizationLifetimeSeconds,
    appVersion: input.appVersion,
    installationHash: await sha256(input.installationId),
    clientHash,
  };

  return jsonResponse(201, {
    schemaVersion: 1,
    authorization: await signClaims(claims, env.SUBMISSION_SIGNING_KEY),
    expiresAt: new Date(claims.expiresAt * 1000).toISOString(),
  });
}

async function createSubmission(request: Request, env: Env): Promise<Response> {
  const parsed = await readJson(request);
  if (parsed instanceof Response) return parsed;

  const validation = validateFeedbackRequest(parsed, env.MINIMUM_APP_VERSION);
  if (validation.error) return validation.error;
  const input = validation.value;

  const claims = await verifyToken(input.authorization, env.SUBMISSION_SIGNING_KEY);
  if (!claims) return errorResponse(401, "invalid_authorization", "Submission authorization is invalid.");

  const now = Math.floor(Date.now() / 1000);
  if (claims.expiresAt < now) {
    return errorResponse(401, "expired_authorization", "Submission authorization has expired.");
  }

  const installationHash = await sha256(input.installationId);
  const clientHash = await hashClient(request);
  if (claims.appVersion !== input.appVersion || claims.installationHash !== installationHash ||
      claims.clientHash !== clientHash) {
    return errorResponse(401, "authorization_mismatch", "Authorization does not match this submission.");
  }

  const submissionLimit = positiveInteger(env.SUBMISSIONS_PER_HOUR, 5);
  const limitedByInstallation = await isRateLimited(
    env.FEEDBACK_DB, "feedback_submissions", "installation_hash", installationHash, now, submissionLimit,
  );
  const limitedByClient = await isRateLimited(
    env.FEEDBACK_DB, "feedback_submissions", "client_hash", clientHash, now, submissionLimit,
  );
  if (limitedByInstallation || limitedByClient) {
    return errorResponse(429, "rate_limited", "Too many feedback submissions. Try again later.", {
      "retry-after": "3600",
    });
  }

  const receiptId = crypto.randomUUID();
  try {
    await env.FEEDBACK_DB.batch([
      env.FEEDBACK_DB.prepare(
        "INSERT INTO consumed_authorizations (token_id, consumed_at) VALUES (?, ?)",
      ).bind(claims.tokenId, now),
      env.FEEDBACK_DB.prepare(
        `INSERT INTO feedback_submissions
         (receipt_id, schema_version, submitted_at, received_at, app_version,
          installation_hash, client_hash, category, message, contact_email,
          screenshot_mime_type, screenshot_base64, client_submission_id)
         VALUES (?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      ).bind(receiptId, input.submittedAt, now, input.appVersion, installationHash,
             clientHash, input.category, input.message, input.contactEmail ?? null,
             input.screenshotMimeType ?? null, input.screenshotBase64 ?? null,
             input.clientSubmissionId),
    ]);
  } catch (error) {
    if (isUniqueConstraintError(error)) {
      const existing = await env.FEEDBACK_DB.prepare(
        `SELECT receipt_id FROM feedback_submissions
           WHERE installation_hash = ? AND client_submission_id = ? LIMIT 1`,
      ).bind(installationHash, input.clientSubmissionId).first<{ receipt_id: string }>();
      if (existing?.receipt_id) {
        return jsonResponse(200, { schemaVersion: 1, receiptId: existing.receipt_id, duplicate: true });
      }
      return errorResponse(409, "duplicate_submission", "This authorization has already been used.");
    }
    throw error;
  }

  return jsonResponse(201, { schemaVersion: 1, receiptId });
}

async function readJson(request: Request): Promise<unknown | Response> {
  const contentType = request.headers.get("content-type")?.toLowerCase() ?? "";
  if (!contentType.startsWith("application/json")) {
    return errorResponse(415, "unsupported_media_type", "Content-Type must be application/json.");
  }

  const declaredLength = Number(request.headers.get("content-length") ?? "0");
  if (declaredLength > maximumBodyBytes) {
    return errorResponse(413, "payload_too_large", "Request body is too large.");
  }

  const body = await request.text();
  if (new TextEncoder().encode(body).byteLength > maximumBodyBytes) {
    return errorResponse(413, "payload_too_large", "Request body is too large.");
  }

  try {
    return JSON.parse(body) as unknown;
  } catch {
    return errorResponse(400, "invalid_json", "Request body is not valid JSON.");
  }
}

function validateAuthorizationRequest(value: unknown, minimumVersion: string):
  { value: AuthorizationRequest; error?: never } | { value?: never; error: Response } {
  if (!isRecord(value) || value.schemaVersion !== 1 || typeof value.appVersion !== "string" ||
      typeof value.installationId !== "string") {
    return { error: errorResponse(400, "invalid_request", "Authorization request fields are invalid.") };
  }
  if (!versionPattern.test(value.appVersion) || compareVersions(value.appVersion, minimumVersion) < 0) {
    return { error: errorResponse(400, "unsupported_app_version", "This application version is not supported.") };
  }
  if (!installationIdPattern.test(value.installationId)) {
    return { error: errorResponse(400, "invalid_installation_id", "Installation identifier is invalid.") };
  }
  return { value: value as unknown as AuthorizationRequest };
}

function validateFeedbackRequest(value: unknown, minimumVersion: string):
  { value: FeedbackRequest; error?: never } | { value?: never; error: Response } {
  const base = validateAuthorizationRequest(value, minimumVersion);
  if (base.error) return { error: base.error };
  if (!isRecord(value) || typeof value.authorization !== "string" ||
      typeof value.submittedAt !== "string" || typeof value.category !== "string" ||
      typeof value.message !== "string" || typeof value.clientSubmissionId !== "string") {
    return { error: errorResponse(400, "invalid_request", "Feedback request fields are invalid.") };
  }
  if (value.authorization.length > 2048 || !allowedCategories.has(value.category)) {
    return { error: errorResponse(400, "invalid_request", "Authorization or category is invalid.") };
  }
  const message = value.message.trim();
  if (!/^[0-9a-f-]{36}$/i.test(value.clientSubmissionId)) {
    return { error: errorResponse(400, "invalid_submission_id", "Submission identifier is invalid.") };
  }
  if (message.length < 3 || message.length > 8000) {
    return { error: errorResponse(400, "invalid_message", "Message must contain 3 to 8000 characters.") };
  }
  const submittedAt = Date.parse(value.submittedAt);
  if (!Number.isFinite(submittedAt) || Math.abs(Date.now() - submittedAt) > 10 * 60 * 1000) {
    return { error: errorResponse(400, "invalid_timestamp", "Submission timestamp is invalid or stale.") };
  }
  if (value.contactEmail !== undefined &&
      (typeof value.contactEmail !== "string" || value.contactEmail.length > 254 ||
       !emailPattern.test(value.contactEmail))) {
    return { error: errorResponse(400, "invalid_contact_email", "Contact email is invalid.") };
  }
  const hasScreenshot = value.screenshotBase64 !== undefined || value.screenshotMimeType !== undefined;
  if (hasScreenshot &&
      ((value.screenshotMimeType !== "image/png" && value.screenshotMimeType !== "image/jpeg") ||
       typeof value.screenshotBase64 !== "string" ||
       value.screenshotBase64.length < 16 ||
       value.screenshotBase64.length > maximumScreenshotBase64Length ||
       !/^[A-Za-z0-9+/]+={0,2}$/.test(value.screenshotBase64))) {
    return { error: errorResponse(400, "invalid_screenshot",
      "Screenshot must be a bounded base64-encoded PNG or JPEG attachment.") };
  }
  return { value: { ...value, message } as unknown as FeedbackRequest };
}

async function isRateLimited(db: D1Database, table: string, column: string, value: string,
                             now: number, limit: number): Promise<boolean> {
  const allowed = new Set(["authorization_requests.client_hash", "feedback_submissions.installation_hash",
                           "feedback_submissions.client_hash"]);
  if (!allowed.has(`${table}.${column}`)) throw new Error("Unsafe rate-limit query");
  const row = await db.prepare(
    `SELECT COUNT(*) AS count FROM ${table} WHERE ${column} = ? AND ${table === "authorization_requests" ? "requested_at" : "received_at"} >= ?`,
  ).bind(value, now - oneHourSeconds).first<{ count: number }>();
  return Number(row?.count ?? 0) >= limit;
}

async function signClaims(claims: TokenClaims, secret: string): Promise<string> {
  const payload = base64UrlEncode(new TextEncoder().encode(JSON.stringify(claims)));
  return `${payload}.${await hmac(payload, secret)}`;
}

async function verifyToken(token: string, secret: string): Promise<TokenClaims | null> {
  const [payload, signature, extra] = token.split(".");
  if (!payload || !signature || extra || !(await timingSafeEqual(signature, await hmac(payload, secret)))) {
    return null;
  }
  try {
    const value = JSON.parse(new TextDecoder().decode(base64UrlDecode(payload))) as TokenClaims;
    return value.version === 1 && typeof value.tokenId === "string" &&
           typeof value.expiresAt === "number" && typeof value.appVersion === "string" &&
           typeof value.installationHash === "string" && typeof value.clientHash === "string"
      ? value : null;
  } catch { return null; }
}

async function hmac(value: string, secret: string): Promise<string> {
  if (secret.length < 32) throw new Error("SUBMISSION_SIGNING_KEY must be at least 32 characters");
  const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return base64UrlEncode(new Uint8Array(await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(value))));
}

async function sha256(value: string): Promise<string> {
  return base64UrlEncode(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value))));
}

async function hashClient(request: Request): Promise<string> {
  const address = request.headers.get("cf-connecting-ip") ?? "unknown";
  return sha256(address);
}

function base64UrlEncode(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function base64UrlDecode(value: string): Uint8Array {
  const padded = value.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
  return Uint8Array.from(atob(padded), (character) => character.charCodeAt(0));
}

async function timingSafeEqual(left: string, right: string): Promise<boolean> {
  if (left.length !== right.length) return false;
  let difference = 0;
  for (let index = 0; index < left.length; ++index) difference |= left.charCodeAt(index) ^ right.charCodeAt(index);
  return difference === 0;
}

function compareVersions(left: string, right: string): number {
  const parse = (version: string) => version.split(/[+-]/, 1)[0].split(".").map(Number);
  const leftParts = parse(left);
  const rightParts = parse(right);
  for (let index = 0; index < 3; ++index) {
    const difference = (leftParts[index] ?? 0) - (rightParts[index] ?? 0);
    if (difference !== 0) return difference;
  }
  return 0;
}

function positiveInteger(value: string, fallback: number): number {
  const parsed = Number.parseInt(value, 10);
  return Number.isInteger(parsed) && parsed > 0 ? parsed : fallback;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isUniqueConstraintError(error: unknown): boolean {
  return error instanceof Error && /UNIQUE constraint failed/i.test(error.message);
}

function jsonResponse(status: number, body: unknown, extraHeaders: HeadersInit = {}): Response {
  return new Response(JSON.stringify(body), { status, headers: { ...jsonHeaders, ...extraHeaders } });
}

function errorResponse(status: number, code: string, message: string,
                       extraHeaders: HeadersInit = {}): Response {
  return jsonResponse(status, { schemaVersion: 1, error: { code, message } }, extraHeaders);
}
