export interface StorageLimits {
  maximumStoredSubmissions: number;
  maximumStoredBytes: number;
  maximumSubmissionsPerInstallation: number;
}

export interface RetentionPolicy {
  resolvedDays: number;
  duplicateDays: number;
  declinedDays: number;
  telemetryDays: number;
  auditDays: number;
}

interface CountRow {
  count: number;
}

interface StorageRow extends CountRow {
  bytes: number;
}

export interface CapacityResult {
  allowed: boolean;
  reason?: "global_count" | "global_bytes" | "installation_count";
}

export interface RetentionResult {
  resolved: number;
  duplicates: number;
  declined: number;
  authorizationRequests: number;
  consumedAuthorizations: number;
  telemetryBuckets: number;
  adminActions: number;
  maintenanceRuns: number;
}

const oneDaySeconds = 24 * 60 * 60;

export function configuredPositiveInteger(value: string | undefined, fallback: number): number {
  const parsed = Number.parseInt(value ?? "", 10);
  return Number.isInteger(parsed) && parsed > 0 ? parsed : fallback;
}

export function storageLimits(env: {
  MAX_STORED_SUBMISSIONS?: string;
  MAX_STORED_BYTES?: string;
  MAX_SUBMISSIONS_PER_INSTALLATION?: string;
}): StorageLimits {
  return {
    maximumStoredSubmissions: configuredPositiveInteger(env.MAX_STORED_SUBMISSIONS, 5000),
    maximumStoredBytes: configuredPositiveInteger(env.MAX_STORED_BYTES, 500_000_000),
    maximumSubmissionsPerInstallation:
      configuredPositiveInteger(env.MAX_SUBMISSIONS_PER_INSTALLATION, 250),
  };
}

export function retentionPolicy(env: {
  RESOLVED_RETENTION_DAYS?: string;
  DUPLICATE_RETENTION_DAYS?: string;
  DECLINED_RETENTION_DAYS?: string;
  TELEMETRY_RETENTION_DAYS?: string;
  AUDIT_RETENTION_DAYS?: string;
}): RetentionPolicy {
  return {
    resolvedDays: configuredPositiveInteger(env.RESOLVED_RETENTION_DAYS, 365),
    duplicateDays: configuredPositiveInteger(env.DUPLICATE_RETENTION_DAYS, 90),
    declinedDays: configuredPositiveInteger(env.DECLINED_RETENTION_DAYS, 30),
    telemetryDays: configuredPositiveInteger(env.TELEMETRY_RETENTION_DAYS, 90),
    auditDays: configuredPositiveInteger(env.AUDIT_RETENTION_DAYS, 730),
  };
}

export function estimatedStoredBytes(input: {
  message: string;
  contactEmail?: string;
  screenshotBase64?: string;
}): number {
  return 512 + input.message.length + (input.contactEmail?.length ?? 0) +
    (input.screenshotBase64?.length ?? 0);
}

export function suspiciousSubmissionReason(message: string): string | null {
  const links = message.match(/https?:\/\//gi)?.length ?? 0;
  if (links >= 3) return "multiple_external_links";
  if (/(.)\1{31,}/s.test(message)) return "repeated_content";
  return null;
}

export async function checkStorageCapacity(
  db: D1Database,
  installationHash: string,
  additionalBytes: number,
  limits: StorageLimits,
): Promise<CapacityResult> {
  const [storage, installation] = await Promise.all([
    db.prepare(
      `SELECT COUNT(*) AS count,
              COALESCE(SUM(LENGTH(message) + LENGTH(COALESCE(contact_email, '')) +
                           LENGTH(COALESCE(screenshot_base64, '')) + 512), 0) AS bytes
         FROM feedback_submissions`,
    ).first<StorageRow>(),
    db.prepare(
      "SELECT COUNT(*) AS count FROM feedback_submissions WHERE installation_hash = ?",
    ).bind(installationHash).first<CountRow>(),
  ]);

  if (Number(storage?.count ?? 0) >= limits.maximumStoredSubmissions) {
    return { allowed: false, reason: "global_count" };
  }
  if (Number(storage?.bytes ?? 0) + additionalBytes > limits.maximumStoredBytes) {
    return { allowed: false, reason: "global_bytes" };
  }
  if (Number(installation?.count ?? 0) >= limits.maximumSubmissionsPerInstallation) {
    return { allowed: false, reason: "installation_count" };
  }
  return { allowed: true };
}

export async function recordRequestMetric(
  db: D1Database,
  route: "authorizations" | "submissions",
  status: number,
  durationMs: number,
  now = Math.floor(Date.now() / 1000),
): Promise<void> {
  const hour = Math.floor(now / 3600) * 3600;
  const outcome = status >= 500 ? "failure" : status >= 400 ? "rejected" : "success";
  await db.prepare(
    `INSERT INTO request_metrics
       (hour, route, outcome, request_count, total_duration_ms, maximum_duration_ms)
     VALUES (?, ?, ?, 1, ?, ?)
     ON CONFLICT(hour, route, outcome) DO UPDATE SET
       request_count = request_count + 1,
       total_duration_ms = total_duration_ms + excluded.total_duration_ms,
       maximum_duration_ms = MAX(maximum_duration_ms, excluded.maximum_duration_ms)`,
  ).bind(hour, route, outcome, Math.max(0, Math.round(durationMs)),
         Math.max(0, Math.round(durationMs))).run();
}

export async function verifyDatabaseAvailability(db: D1Database): Promise<boolean> {
  const row = await db.prepare("SELECT 1 AS available").first<{ available: number }>();
  return row?.available === 1;
}

function changes(result: D1Result): number {
  return Number(result.meta?.changes ?? 0);
}

export async function runRetention(
  db: D1Database,
  policy: RetentionPolicy,
  actor: string,
  now = Math.floor(Date.now() / 1000),
): Promise<RetentionResult> {
  const cutoffs = {
    resolved: now - policy.resolvedDays * oneDaySeconds,
    duplicate: now - policy.duplicateDays * oneDaySeconds,
    declined: now - policy.declinedDays * oneDaySeconds,
    telemetry: now - policy.telemetryDays * oneDaySeconds,
    audit: now - policy.auditDays * oneDaySeconds,
    ephemeral: now - 2 * oneDaySeconds,
  };

  await db.prepare(
    `UPDATE feedback_submissions
        SET duplicate_of = NULL, status = 'needs_review'
      WHERE duplicate_of IN (
        SELECT receipt_id FROM feedback_submissions
         WHERE (status = 'resolved' AND received_at < ?)
            OR (status = 'duplicate' AND received_at < ?)
            OR (status = 'declined' AND received_at < ?)
      )`,
  ).bind(cutoffs.resolved, cutoffs.duplicate, cutoffs.declined).run();

  const duplicateResult = await db.prepare(
    "DELETE FROM feedback_submissions WHERE status = 'duplicate' AND received_at < ?",
  ).bind(cutoffs.duplicate).run();
  const declinedResult = await db.prepare(
    "DELETE FROM feedback_submissions WHERE status = 'declined' AND received_at < ?",
  ).bind(cutoffs.declined).run();
  const resolvedResult = await db.prepare(
    "DELETE FROM feedback_submissions WHERE status = 'resolved' AND received_at < ?",
  ).bind(cutoffs.resolved).run();
  const authorizationResult = await db.prepare(
    "DELETE FROM authorization_requests WHERE requested_at < ?",
  ).bind(cutoffs.ephemeral).run();
  const consumedResult = await db.prepare(
    "DELETE FROM consumed_authorizations WHERE consumed_at < ?",
  ).bind(cutoffs.ephemeral).run();
  const telemetryResult = await db.prepare(
    "DELETE FROM request_metrics WHERE hour < ?",
  ).bind(cutoffs.telemetry).run();
  const adminActionsResult = await db.prepare(
    "DELETE FROM admin_action_receipts WHERE created_at < ?",
  ).bind(cutoffs.audit).run();
  const maintenanceResult = await db.prepare(
    "DELETE FROM maintenance_runs WHERE completed_at < ?",
  ).bind(cutoffs.audit).run();

  const result: RetentionResult = {
    resolved: changes(resolvedResult),
    duplicates: changes(duplicateResult),
    declined: changes(declinedResult),
    authorizationRequests: changes(authorizationResult),
    consumedAuthorizations: changes(consumedResult),
    telemetryBuckets: changes(telemetryResult),
    adminActions: changes(adminActionsResult),
    maintenanceRuns: changes(maintenanceResult),
  };

  await db.prepare(
    `INSERT INTO maintenance_runs (operation, actor, started_at, completed_at, details_json)
     VALUES ('retention', ?, ?, ?, ?)`,
  ).bind(actor, now, Math.floor(Date.now() / 1000), JSON.stringify(result)).run();
  return result;
}

export async function operationalReport(db: D1Database, now = Math.floor(Date.now() / 1000)) {
  const since = now - oneDaySeconds;
  const [storage, telemetry, delay, maintenance] = await Promise.all([
    db.prepare(
      `SELECT COUNT(*) AS count,
              COALESCE(SUM(LENGTH(message) + LENGTH(COALESCE(contact_email, '')) +
                           LENGTH(COALESCE(screenshot_base64, '')) + 512), 0) AS bytes,
              SUM(CASE WHEN quarantine_reason IS NOT NULL THEN 1 ELSE 0 END) AS quarantined
         FROM feedback_submissions`,
    ).first<{ count: number; bytes: number; quarantined: number }>(),
    db.prepare(
      `SELECT COALESCE(SUM(request_count), 0) AS requests,
              COALESCE(SUM(CASE WHEN outcome = 'failure' THEN request_count ELSE 0 END), 0)
                AS failures,
              COALESCE(SUM(CASE WHEN outcome = 'rejected' THEN request_count ELSE 0 END), 0)
                AS rejected,
              COALESCE(SUM(total_duration_ms), 0) AS total_duration_ms,
              COALESCE(MAX(maximum_duration_ms), 0) AS maximum_duration_ms
         FROM request_metrics WHERE hour >= ?`,
    ).bind(since).first<{
      requests: number; failures: number; rejected: number;
      total_duration_ms: number; maximum_duration_ms: number;
    }>(),
    db.prepare(
      `SELECT COALESCE(AVG(MAX(0, received_at - CAST(strftime('%s', submitted_at) AS INTEGER))), 0)
                AS average_seconds,
              COALESCE(MAX(MAX(0, received_at - CAST(strftime('%s', submitted_at) AS INTEGER))), 0)
                AS maximum_seconds
         FROM feedback_submissions WHERE received_at >= ?`,
    ).bind(since).first<{ average_seconds: number; maximum_seconds: number }>(),
    db.prepare(
      `SELECT completed_at, details_json FROM maintenance_runs
        WHERE operation = 'retention' ORDER BY completed_at DESC LIMIT 1`,
    ).first<{ completed_at: number; details_json: string }>(),
  ]);

  const requests = Number(telemetry?.requests ?? 0);
  const failures = Number(telemetry?.failures ?? 0);
  let maintenanceDetails: unknown = null;
  try {
    maintenanceDetails = maintenance?.details_json ? JSON.parse(maintenance.details_json) : null;
  } catch {
    maintenanceDetails = null;
  }

  return {
    windowHours: 24,
    availabilityPercent: requests ? ((requests - failures) / requests) * 100 : null,
    requests,
    failures,
    rejected: Number(telemetry?.rejected ?? 0),
    averageResponseMs: requests ? Number(telemetry?.total_duration_ms ?? 0) / requests : 0,
    maximumResponseMs: Number(telemetry?.maximum_duration_ms ?? 0),
    averageProcessingDelaySeconds: Number(delay?.average_seconds ?? 0),
    maximumProcessingDelaySeconds: Number(delay?.maximum_seconds ?? 0),
    storedSubmissions: Number(storage?.count ?? 0),
    estimatedStoredBytes: Number(storage?.bytes ?? 0),
    quarantinedSubmissions: Number(storage?.quarantined ?? 0),
    lastRetentionAt: maintenance?.completed_at
      ? new Date(maintenance.completed_at * 1000).toISOString()
      : null,
    lastRetentionResult: maintenanceDetails,
  };
}
