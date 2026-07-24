import adminPageTemplate from "./admin.html";
import adminStyles from "./admin.css";
import adminScript from "./dashboard.js";
import auditPageTemplate from "./audit.html";
import auditScript from "./audit.js";
import { operationalReport, retentionPolicy, runRetention } from "./operations";

export interface AdminEnv {
  FEEDBACK_DB: D1Database;
  ADMIN_EMAILS: string;
  RESOLVED_RETENTION_DAYS?: string;
  DUPLICATE_RETENTION_DAYS?: string;
  DECLINED_RETENTION_DAYS?: string;
  TELEMETRY_RETENTION_DAYS?: string;
  AUDIT_RETENTION_DAYS?: string;
}

interface SubmissionRow {
  receipt_id: string;
  submitted_at: string;
  received_at: number;
  app_version: string;
  category: string;
  message: string;
  contact_email: string | null;
  status: string;
  developer_notes: string;
  priority: string | null;
  tags_json: string;
  github_issue_url: string | null;
  duplicate_of: string | null;
  screenshot_mime_type: string | null;
  quarantine_reason: string | null;
  quarantined_at: number | null;
}

const statuses = new Set(["new", "needs_review", "planned", "duplicate", "resolved", "declined"]);
const priorities = new Set(["low", "medium", "high", "critical"]);
const categories = new Set(["bug", "idea", "usability", "other"]);
const versionPattern = /^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/;
const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const jsonHeaders = { "content-type": "application/json; charset=utf-8" };

export async function handleAdminRequest(request: Request, env: AdminEnv): Promise<Response> {
  const user = authenticatedUser(request, env.ADMIN_EMAILS);
  if (!user) {
    return new Response("Administrative access requires an authorized Cloudflare Access identity.", {
      status: 401,
      headers: { "cache-control": "no-store", "content-type": "text/plain; charset=utf-8" },
    });
  }

  const url = new URL(request.url);
  if (url.pathname === "/admin.css" && request.method === "GET") {
    return assetResponse(adminStyles, "text/css; charset=utf-8");
  }
  if (url.pathname === "/admin.js" && request.method === "GET") {
    return assetResponse(adminScript, "text/javascript; charset=utf-8");
  }
  if (url.pathname === "/audit.js" && request.method === "GET") {
    return assetResponse(auditScript, "text/javascript; charset=utf-8");
  }
  if (url.pathname === "/admin/audit" && request.method === "GET") {
    return htmlResponse(auditPageTemplate.replace("{{ADMIN_USER}}", escapeHtml(user)));
  }
  if (url.pathname === "/admin" && request.method === "GET") {
    return htmlResponse(adminPage(user));
  }
  if (url.pathname === "/v1/admin/submissions" && request.method === "GET") {
    return listSubmissions(url, env.FEEDBACK_DB);
  }
  if (url.pathname === "/v1/admin/submissions" && request.method === "POST") {
    return createSubmission(request, env.FEEDBACK_DB, user);
  }
  if (url.pathname === "/v1/admin/audit" && request.method === "GET") {
    return listAdminActions(url, env.FEEDBACK_DB);
  }
  if (url.pathname === "/v1/admin/operations" && request.method === "GET") {
    return jsonResponse(200, { operations: await operationalReport(env.FEEDBACK_DB) });
  }
  if (url.pathname === "/v1/admin/maintenance/retention" && request.method === "POST") {
    const result = await runRetention(env.FEEDBACK_DB, retentionPolicy(env), user);
    return jsonResponse(200, { retention: result });
  }
  if (url.pathname === "/v1/admin/submissions/batch-delete" && request.method === "POST") {
    return deleteSubmissions(request, env.FEEDBACK_DB, user);
  }
  if (url.pathname === "/v1/admin/export" && request.method === "GET") {
    return exportSubmissions(url, env.FEEDBACK_DB);
  }
  const screenshotMatch = url.pathname.match(
    /^\/v1\/admin\/submissions\/([0-9a-f-]{36})\/screenshot$/i,
  );
  if (screenshotMatch && request.method === "GET") {
    return getSubmissionScreenshot(screenshotMatch[1], env.FEEDBACK_DB);
  }
  const match = url.pathname.match(/^\/v1\/admin\/submissions\/([0-9a-f-]{36})$/i);
  if (match && request.method === "PATCH") {
    return updateSubmission(match[1], request, env.FEEDBACK_DB, user);
  }
  if (match && request.method === "GET") {
    return getSubmission(match[1], env.FEEDBACK_DB);
  }
  if (match && request.method === "DELETE") {
    return deleteSubmission(match[1], env.FEEDBACK_DB, user);
  }
  return jsonResponse(404, { error: { code: "not_found", message: "Administrative route not found." } });
}

function authenticatedUser(request: Request, configuredEmails: string): string | null {
  const email = request.headers.get("cf-access-authenticated-user-email")?.trim().toLowerCase();
  const allowed = configuredEmails.split(",").map((value) => value.trim().toLowerCase()).filter(Boolean);
  return email && allowed.includes(email) ? email : null;
}

async function listSubmissions(url: URL, db: D1Database): Promise<Response> {
  const { where, values } = filters(url);
  const limit = Math.min(Math.max(Number.parseInt(url.searchParams.get("limit") ?? "100", 10) || 100, 1), 250);
  const result = await db.prepare(
    `SELECT receipt_id, submitted_at, received_at, app_version, category, message, contact_email,
            status, developer_notes, priority, tags_json, github_issue_url, duplicate_of,
            screenshot_mime_type, quarantine_reason, quarantined_at
       FROM feedback_submissions ${where} ORDER BY received_at DESC LIMIT ?`,
  ).bind(...values, limit).all<SubmissionRow>();
  return jsonResponse(200, { submissions: (result.results ?? []).map(presentSubmission) });
}

async function getSubmission(receiptId: string, db: D1Database): Promise<Response> {
  const result = await db.prepare(
    `SELECT receipt_id, submitted_at, received_at, app_version, category, message, contact_email,
            status, developer_notes, priority, tags_json, github_issue_url, duplicate_of,
            screenshot_mime_type, quarantine_reason, quarantined_at
       FROM feedback_submissions WHERE receipt_id = ? LIMIT 1`,
  ).bind(receiptId).all<SubmissionRow>();
  const submission = result.results?.[0];
  return submission
    ? jsonResponse(200, { submission: presentSubmission(submission) })
    : jsonResponse(404, { error: { code: "not_found", message: "Submission not found." } });
}

async function getSubmissionScreenshot(receiptId: string, db: D1Database): Promise<Response> {
  const result = await db.prepare(
    `SELECT screenshot_mime_type, screenshot_base64
       FROM feedback_submissions WHERE receipt_id = ? LIMIT 1`,
  ).bind(receiptId).all<{ screenshot_mime_type: string | null; screenshot_base64: string | null }>();
  const screenshot = result.results?.[0];
  if (!screenshot?.screenshot_mime_type || !screenshot.screenshot_base64) {
    return jsonResponse(404, { error: { code: "screenshot_not_found", message: "Screenshot not found." } });
  }

  const binary = atob(screenshot.screenshot_base64);
  const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
  return new Response(bytes, {
    headers: {
      "cache-control": "no-store",
      "content-disposition": `inline; filename="feedback-${receiptId}.jpg"`,
      "content-type": screenshot.screenshot_mime_type,
      "x-content-type-options": "nosniff",
    },
  });
}

async function createSubmission(request: Request, db: D1Database, adminEmail: string): Promise<Response> {
  const parsed = await readObject(request);
  if (parsed instanceof Response) return parsed;
  const core = validateCoreFields(parsed, true);
  if (core instanceof Response) return core;
  const status = typeof parsed.status === "string" ? parsed.status : "new";
  if (!statuses.has(status)) return invalid("Unknown status.");
  const submittedAt = typeof parsed.submittedAt === "string" ? parsed.submittedAt : new Date().toISOString();
  if (!Number.isFinite(Date.parse(submittedAt))) return invalid("Submission date is invalid.");
  const diagnostic = typeof parsed.diagnosticContext === "string" ? parsed.diagnosticContext.trim() : "";
  if (diagnostic.length > 1000) return invalid("Diagnostic context is too long.");
  const message = diagnostic ? `${core.message}\nEnvironment: ${diagnostic}` : core.message;
  const receiptId = crypto.randomUUID();
  const receivedAt = Math.floor(Date.now() / 1000);
  const result = await db.prepare(
    `INSERT INTO feedback_submissions
       (receipt_id, schema_version, submitted_at, received_at, app_version, installation_hash,
        client_hash, category, message, contact_email, status)
     VALUES (?, 1, ?, ?, ?, 'manual', 'manual', ?, ?, ?, ?)`,
  ).bind(receiptId, submittedAt, receivedAt, core.appVersion, core.category, message,
          core.contactEmail, status).run();
  if (!result.success) return jsonResponse(500, { error: { code: "create_failed", message: "Submission was not created." } });
  await recordAdminAction(db, adminEmail, "create", receiptId, {
    category: core.category, appVersion: core.appVersion, status,
  });
  return jsonResponse(201, { receiptId, created: true });
}

function filters(url: URL): { where: string; values: unknown[] } {
  const clauses: string[] = [];
  const values: unknown[] = [];
  const addExact = (parameter: string, column: string) => {
    const value = url.searchParams.get(parameter)?.trim();
    if (value) { clauses.push(`${column} = ?`); values.push(value); }
  };
  addExact("category", "category");
  addExact("appVersion", "app_version");
  addExact("status", "status");
  addExact("priority", "priority");
  if (url.searchParams.get("quarantined") === "true") {
    clauses.push("quarantine_reason IS NOT NULL");
  }
  const query = url.searchParams.get("q")?.trim();
  if (query) {
    clauses.push("(message LIKE ? OR developer_notes LIKE ? OR tags_json LIKE ? OR contact_email LIKE ?)");
    const pattern = `%${query.replace(/[\\%_]/g, "\\$&")}%`;
    values.push(pattern, pattern, pattern, pattern);
  }
  const platform = url.searchParams.get("platform")?.trim();
  if (platform) { clauses.push("message LIKE ?"); values.push(`%Environment:%${platform}%`); }
  const from = Date.parse(url.searchParams.get("from") ?? "");
  if (Number.isFinite(from)) { clauses.push("received_at >= ?"); values.push(Math.floor(from / 1000)); }
  const to = Date.parse(url.searchParams.get("to") ?? "");
  if (Number.isFinite(to)) { clauses.push("received_at <= ?"); values.push(Math.floor(to / 1000) + 86399); }
  return { where: clauses.length ? `WHERE ${clauses.join(" AND ")}` : "", values };
}

async function updateSubmission(receiptId: string, request: Request, db: D1Database,
                                adminEmail: string): Promise<Response> {
  const input = await readObject(request);
  if (input instanceof Response) return input;

  const updates: string[] = [];
  const values: unknown[] = [];
  const set = (column: string, value: unknown) => { updates.push(`${column} = ?`); values.push(value); };
  if (input.category !== undefined) {
    if (typeof input.category !== "string" || !categories.has(input.category)) return invalid("Unknown category.");
    set("category", input.category);
  }
  if (input.appVersion !== undefined) {
    if (typeof input.appVersion !== "string" || !versionPattern.test(input.appVersion)) return invalid("Application version is invalid.");
    set("app_version", input.appVersion);
  }
  if (input.message !== undefined) {
    if (typeof input.message !== "string" || input.message.trim().length < 3 || input.message.length > 8000) {
      return invalid("Message must contain 3 to 8000 characters.");
    }
    set("message", input.message.trim());
  }
  if (input.contactEmail !== undefined) {
    if (input.contactEmail !== null &&
        (typeof input.contactEmail !== "string" || input.contactEmail.length > 254 || !emailPattern.test(input.contactEmail))) {
      return invalid("Contact email is invalid.");
    }
    set("contact_email", input.contactEmail || null);
  }
  if (input.status !== undefined) {
    if (typeof input.status !== "string" || !statuses.has(input.status)) return invalid("Unknown status.");
    set("status", input.status);
  }
  if (input.priority !== undefined) {
    if (input.priority !== null && (typeof input.priority !== "string" || !priorities.has(input.priority))) {
      return invalid("Unknown priority.");
    }
    set("priority", input.priority);
  }
  if (input.developerNotes !== undefined) {
    if (typeof input.developerNotes !== "string" || input.developerNotes.length > 8000) return invalid("Developer notes are too long.");
    set("developer_notes", input.developerNotes.trim());
  }
  if (input.tags !== undefined) {
    if (!Array.isArray(input.tags) || input.tags.length > 20 ||
        input.tags.some((tag) => typeof tag !== "string" || tag.trim().length < 1 || tag.length > 40)) {
      return invalid("Tags must contain at most 20 values of 1 to 40 characters.");
    }
    set("tags_json", JSON.stringify([...new Set(input.tags.map((tag) => (tag as string).trim()))]));
  }
  if (input.githubIssueUrl !== undefined) {
    if (input.githubIssueUrl !== null &&
        (typeof input.githubIssueUrl !== "string" || !/^https:\/\/github\.com\/[^/]+\/[^/]+\/issues\/\d+$/.test(input.githubIssueUrl))) {
      return invalid("GitHub issue URL is invalid.");
    }
    set("github_issue_url", input.githubIssueUrl || null);
  }
  if (input.duplicateOf !== undefined) {
    if (input.duplicateOf !== null &&
        (typeof input.duplicateOf !== "string" || !/^[0-9a-f-]{36}$/i.test(input.duplicateOf) || input.duplicateOf === receiptId)) {
      return invalid("Duplicate target is invalid.");
    }
    set("duplicate_of", input.duplicateOf);
    if (input.duplicateOf) set("status", "duplicate");
  }
  if (input.quarantineReason !== undefined) {
    if (input.quarantineReason !== null &&
        (typeof input.quarantineReason !== "string" ||
         input.quarantineReason.trim().length < 1 || input.quarantineReason.length > 120)) {
      return invalid("Quarantine reason must contain 1 to 120 characters.");
    }
    const reason = typeof input.quarantineReason === "string"
      ? input.quarantineReason.trim()
      : null;
    set("quarantine_reason", reason);
    set("quarantined_at", reason ? Math.floor(Date.now() / 1000) : null);
    if (reason) set("status", "needs_review");
  }
  if (!updates.length) return invalid("No supported fields were supplied.");

  values.push(receiptId);
  const result = await db.prepare(`UPDATE feedback_submissions SET ${updates.join(", ")} WHERE receipt_id = ?`)
    .bind(...values).run();
  if (!result.meta.changes) return jsonResponse(404, { error: { code: "not_found", message: "Submission not found." } });
  await recordAdminAction(db, adminEmail, "update", receiptId, { fields: Object.keys(input).sort() });
  return jsonResponse(200, { receiptId, updated: true });
}

async function deleteSubmission(receiptId: string, db: D1Database, adminEmail: string): Promise<Response> {
  const title = await submissionTitle(db, receiptId);
  await db.prepare(
    "UPDATE feedback_submissions SET duplicate_of = NULL, status = 'needs_review' WHERE duplicate_of = ?",
  ).bind(receiptId).run();
  const result = await db.prepare("DELETE FROM feedback_submissions WHERE receipt_id = ?").bind(receiptId).run();
  if (!result.meta.changes) return jsonResponse(404, { error: { code: "not_found", message: "Submission not found." } });
  await recordAdminAction(db, adminEmail, "delete", receiptId, { title });
  return new Response(null, { status: 204, headers: { "cache-control": "no-store" } });
}

async function deleteSubmissions(request: Request, db: D1Database, adminEmail: string): Promise<Response> {
  const input = await readObject(request);
  if (input instanceof Response) return input;
  if (!Array.isArray(input.receiptIds) || input.receiptIds.length < 1 || input.receiptIds.length > 250 ||
      input.receiptIds.some((id) => typeof id !== "string" || !/^[0-9a-f-]{36}$/i.test(id))) {
    return invalid("Select between 1 and 250 valid feedback records.");
  }

  const receiptIds = [...new Set(input.receiptIds as string[])];
  const deleted: string[] = [];
  const missing: string[] = [];
  for (const receiptId of receiptIds) {
    const title = await submissionTitle(db, receiptId);
    await db.prepare(
      "UPDATE feedback_submissions SET duplicate_of = NULL, status = 'needs_review' WHERE duplicate_of = ?",
    ).bind(receiptId).run();
    const result = await db.prepare("DELETE FROM feedback_submissions WHERE receipt_id = ?").bind(receiptId).run();
    if (!result.meta.changes) {
      missing.push(receiptId);
      continue;
    }
    await recordAdminAction(db, adminEmail, "delete", receiptId, { batch: true, title });
    deleted.push(receiptId);
  }

  return jsonResponse(200, { deleted, missing });
}

async function submissionTitle(db: D1Database, receiptId: string): Promise<string> {
  const result = await db.prepare(
    "SELECT message FROM feedback_submissions WHERE receipt_id = ? LIMIT 1",
  ).bind(receiptId).all<{ message: string }>();
  const message = result.results?.[0]?.message;
  return message ? parseStructuredFeedback(message).title || "Untitled feedback" : "Unknown feedback";
}

async function recordAdminAction(db: D1Database, adminEmail: string, action: string,
                                 receiptId: string, details: Record<string, unknown>): Promise<void> {
  await db.prepare(
    `INSERT INTO admin_action_receipts (admin_email, action, receipt_id, details_json, created_at)
     VALUES (?, ?, ?, ?, ?)`,
  ).bind(adminEmail, action, receiptId, JSON.stringify(details), Math.floor(Date.now() / 1000)).run();
}

async function listAdminActions(url: URL, db: D1Database): Promise<Response> {
  const limit = Math.min(Math.max(Number.parseInt(url.searchParams.get("limit") ?? "100", 10) || 100, 1), 250);
  const result = await db.prepare(
    `SELECT id, admin_email, action, receipt_id, details_json, created_at
       FROM admin_action_receipts ORDER BY created_at DESC, id DESC LIMIT ?`,
  ).bind(limit).all<{
    id: number; admin_email: string; action: string; receipt_id: string;
    details_json: string; created_at: number;
  }>();
  return jsonResponse(200, { actions: (result.results ?? []).map((row) => {
    let details: unknown = {};
    try { details = JSON.parse(row.details_json); } catch { /* keep malformed legacy details empty */ }
    return {
      id: row.id, adminEmail: row.admin_email, action: row.action, receiptId: row.receipt_id,
      details, createdAt: new Date(row.created_at * 1000).toISOString(),
    };
  }) });
}

function validateCoreFields(input: Record<string, unknown>, required: boolean):
  { category: string; appVersion: string; message: string; contactEmail: string | null } | Response {
  if ((required || input.category !== undefined) &&
      (typeof input.category !== "string" || !categories.has(input.category))) return invalid("Unknown category.");
  if ((required || input.appVersion !== undefined) &&
      (typeof input.appVersion !== "string" || !versionPattern.test(input.appVersion))) return invalid("Application version is invalid.");
  if ((required || input.message !== undefined) &&
      (typeof input.message !== "string" || input.message.trim().length < 3 || input.message.length > 8000)) {
    return invalid("Message must contain 3 to 8000 characters.");
  }
  if (input.contactEmail !== undefined && input.contactEmail !== null &&
      (typeof input.contactEmail !== "string" || input.contactEmail.length > 254 || !emailPattern.test(input.contactEmail))) {
    return invalid("Contact email is invalid.");
  }
  return {
    category: input.category as string, appVersion: input.appVersion as string,
    message: (input.message as string).trim(), contactEmail: (input.contactEmail as string | null) || null,
  };
}

async function readObject(request: Request): Promise<Record<string, unknown> | Response> {
  let input: unknown;
  try { input = await request.json(); } catch { return invalid("Request body must be valid JSON."); }
  return isRecord(input) ? input : invalid("Request body must be an object.");
}

async function exportSubmissions(url: URL, db: D1Database): Promise<Response> {
  const ids = [...new Set(url.searchParams.getAll("id"))].filter((id) => /^[0-9a-f-]{36}$/i.test(id));
  if (!ids.length || ids.length > 250) return invalid("Select between 1 and 250 submissions to export.");
  const placeholders = ids.map(() => "?").join(",");
  const result = await db.prepare(
    `SELECT receipt_id, submitted_at, received_at, app_version, category, message, contact_email,
            status, developer_notes, priority, tags_json, github_issue_url, duplicate_of,
            quarantine_reason, quarantined_at
       FROM feedback_submissions WHERE receipt_id IN (${placeholders}) ORDER BY received_at DESC`,
  ).bind(...ids).all<SubmissionRow>();
  const columns: (keyof SubmissionRow)[] = ["receipt_id", "submitted_at", "received_at", "app_version", "category",
    "message", "contact_email", "status", "developer_notes", "priority", "tags_json",
    "github_issue_url", "duplicate_of", "quarantine_reason", "quarantined_at"];
  const csv = [columns.join(","), ...(result.results ?? []).map((row) =>
    columns.map((column) => csvCell(row[column])).join(","))].join("\r\n");
  return new Response(csv, { headers: {
    "cache-control": "no-store", "content-disposition": "attachment; filename=practice-takes-feedback.csv",
    "content-type": "text/csv; charset=utf-8",
  } });
}

function presentSubmission(row: SubmissionRow) {
  const contextMarker = "\n\nFeedback context (user-editable):\n";
  const contextIndex = row.message.lastIndexOf(contextMarker);
  const contextTag = contextIndex < 0
    ? null
    : row.message.slice(contextIndex + contextMarker.length).trim();
  const messageWithoutContext = contextIndex < 0 ? row.message : row.message.slice(0, contextIndex);
  const marker = "\nEnvironment: ";
  const markerIndex = messageWithoutContext.lastIndexOf(marker);
  const userFeedback = markerIndex < 0
    ? messageWithoutContext
    : messageWithoutContext.slice(0, markerIndex);
  const diagnosticContext = markerIndex < 0
    ? null
    : messageWithoutContext.slice(markerIndex + marker.length).trim();
  const structuredFeedback = parseStructuredFeedback(userFeedback);
  let tags: unknown = [];
  try { tags = JSON.parse(row.tags_json); } catch { /* preserve an empty list for malformed legacy data */ }
  return {
    receiptId: row.receipt_id, submittedAt: row.submitted_at,
    receivedAt: new Date(row.received_at * 1000).toISOString(), appVersion: row.app_version,
    category: row.category, userFeedback, diagnosticContext, contactEmail: row.contact_email,
    contextTag,
    hasScreenshot: Boolean(row.screenshot_mime_type),
    feedbackType: structuredFeedback.type, title: structuredFeedback.title,
    description: structuredFeedback.description, contactSummary: structuredFeedback.contact,
    status: row.status, developerNotes: row.developer_notes, priority: row.priority, tags,
    githubIssueUrl: row.github_issue_url, duplicateOf: row.duplicate_of,
    quarantineReason: row.quarantine_reason,
    quarantinedAt: row.quarantined_at ? new Date(row.quarantined_at * 1000).toISOString() : null,
  };
}

function parseStructuredFeedback(message: string): {
  type: string; title: string; description: string; contact: string;
} {
  const match = message.match(
    /^Type:\s*(.*?)\nTitle:\s*(.*?)\n\nDescription:\n([\s\S]*?)(?:\n\nContact:\s*([\s\S]*))?$/,
  );
  if (!match) return { type: "", title: "", description: message, contact: "" };
  return {
    type: match[1]?.trim() ?? "",
    title: match[2]?.trim() ?? "",
    description: match[3]?.trim() ?? "",
    contact: match[4]?.trim() ?? "",
  };
}

function csvCell(value: unknown): string {
  const text = value === null || value === undefined ? "" : String(value);
  return `"${text.replace(/"/g, '""')}"`;
}

function invalid(message: string): Response {
  return jsonResponse(400, { error: { code: "invalid_request", message } });
}

function jsonResponse(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), { status, headers: { ...jsonHeaders, "cache-control": "no-store" } });
}

function assetResponse(body: string, contentType: string): Response {
  return new Response(body, { headers: {
    "cache-control": "no-store", "content-type": contentType, "x-content-type-options": "nosniff",
  } });
}

function htmlResponse(body: string): Response {
  return new Response(body, { headers: {
    "cache-control": "no-store",
    "content-security-policy":
      "default-src 'self'; img-src 'self' blob:; style-src 'self'; script-src 'self'; frame-ancestors 'none'",
    "content-type": "text/html; charset=utf-8", "x-content-type-options": "nosniff",
  } });
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function adminPage(user: string): string {
  return adminPageTemplate.replace("{{ADMIN_USER}}", escapeHtml(user));
}

function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (character) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[character]!);
}
