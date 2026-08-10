import { configuredPositiveInteger } from "./operations";

export interface NotificationEnv {
  FEEDBACK_EMAIL?: SendEmail;
  FEEDBACK_NOTIFICATION_FROM?: string;
  FEEDBACK_NOTIFICATION_TO?: string;
  FEEDBACK_DASHBOARD_URL?: string;
  FEEDBACK_MAX_DAILY_EMAILS?: string;
  ADMIN_EMAILS?: string;
}

interface PendingFeedback {
  receipt_id: string;
  received_at: number;
  app_version: string;
  category: string;
  message: string;
  contact_email: string | null;
  screenshot_mime_type: string | null;
}

/**
 * Why a dispatch sent what it sent. Every one of these used to be the number
 * zero, which is why a delivery path that had never worked could not be told
 * apart from an empty queue.
 */
export type DispatchOutcome =
  | "sent"
  | "nothing_pending"
  | "not_configured"
  | "daily_limit_reached"
  | "send_failed";

export interface DispatchResult {
  outcome: DispatchOutcome;
  sent: number;
  /** Reports still queued, or null when the dispatch stopped before counting. */
  pending: number | null;
  dailyLimit: number;
  dailyEmailNumber: number | null;
  remainingDailyEmails: number | null;
  messageId: string | null;
  problems: string[];
  error: string | null;
}

export interface DispatchAttempt {
  actor: string;
  outcome: DispatchOutcome;
  completedAt: string;
  sent: number;
  problems: string[];
  error: string | null;
}

export interface NotificationStatus {
  configured: boolean;
  problems: string[];
  from: string | null;
  to: string | null;
  dashboardUrl: string | null;
  queue: {
    pending: number;
    claimed: number;
    oldestReceivedAt: string | null;
  };
  daily: {
    day: string;
    sent: number;
    limit: number;
    remaining: number;
  };
  lastAttempt: DispatchAttempt | null;
}

interface NotificationConfiguration {
  email: SendEmail;
  from: string;
  to: string;
  dashboardUrl: string;
}

type ConfigurationResult =
  | { configuration: NotificationConfiguration; problems: [] }
  | { configuration: null; problems: string[] };

const defaultDailyEmails = 3;
// Feedback messages are limited to 8,000 UTF-16 code units. Capping a batch
// at 100 keeps even worst-case UTF-8 content safely below Cloudflare Email
// Service's 5 MiB total-message limit after headers and formatting.
const maximumFeedbackPerEmail = 100;
const staleClaimSeconds = 30 * 60;
const maximumEmailAddressLength = 254;
const maximumDashboardUrlLength = 2048;

// Domains that can never be onboarded to Cloudflare Email Sending, so a sender
// on one of them is a configuration mistake rather than a delivery failure
// waiting to be diagnosed. `workers.dev` is included because the service's own
// hostname is the address most likely to be reached for by mistake.
const unsendableDomains = new Set([
  "example.com",
  "example.org",
  "example.net",
  "example.edu",
  "localhost",
]);
const unsendableSuffixes = [".invalid", ".test", ".local", ".localhost", ".example",
                            ".example.com", ".example.org", ".example.net", ".example.edu",
                            ".workers.dev"];

export function dailyEmailLimit(env: NotificationEnv): number {
  return configuredPositiveInteger(env.FEEDBACK_MAX_DAILY_EMAILS, defaultDailyEmails);
}

export async function sendPendingFeedbackBatch(
  db: D1Database,
  env: NotificationEnv,
  actor: string,
  now = new Date(),
): Promise<DispatchResult> {
  const startedAt = Math.floor(now.getTime() / 1000);
  const result = await dispatch(db, env, now);
  await recordDispatchAttempt(db, actor, startedAt, result);
  return result;
}

async function dispatch(
  db: D1Database,
  env: NotificationEnv,
  now: Date,
): Promise<DispatchResult> {
  const dailyLimit = dailyEmailLimit(env);
  const empty: DispatchResult = {
    outcome: "nothing_pending",
    sent: 0,
    pending: null,
    dailyLimit,
    dailyEmailNumber: null,
    remainingDailyEmails: null,
    messageId: null,
    problems: [],
    error: null,
  };

  const { configuration, problems } = notificationConfiguration(env);
  if (!configuration) {
    return { ...empty, outcome: "not_configured", problems };
  }

  const nowSeconds = Math.floor(now.getTime() / 1000);
  await db.prepare(
    `UPDATE feedback_email_queue
        SET claim_id = NULL, claimed_at = NULL
      WHERE claim_id IS NOT NULL AND claimed_at < ?`,
  ).bind(nowSeconds - staleClaimSeconds).run();

  const pending = await db.prepare(
    `SELECT COUNT(*) AS count
       FROM feedback_email_queue
      WHERE claim_id IS NULL`,
  ).first<{ count: number }>();
  const pendingCount = Number(pending?.count ?? 0);
  if (pendingCount < 1) return { ...empty, pending: 0 };

  const notificationDay = now.toISOString().slice(0, 10);
  const reservation = await db.prepare(
    `INSERT INTO feedback_notification_days (notification_day, sent_count, updated_at)
     VALUES (?, 1, ?)
     ON CONFLICT(notification_day) DO UPDATE SET
       sent_count = feedback_notification_days.sent_count + 1,
       updated_at = excluded.updated_at
     WHERE feedback_notification_days.sent_count < ?
     RETURNING sent_count`,
  ).bind(notificationDay, nowSeconds, dailyLimit)
    .first<{ sent_count: number }>();
  if (!reservation) {
    return {
      ...empty,
      outcome: "daily_limit_reached",
      pending: pendingCount,
      remainingDailyEmails: 0,
    };
  }

  const remainingSlots = dailyLimit - reservation.sent_count + 1;
  const batchSize = Math.min(
    Math.ceil(pendingCount / remainingSlots),
    maximumFeedbackPerEmail,
  );
  const claimId = crypto.randomUUID();
  const claimed = await db.prepare(
    `UPDATE feedback_email_queue
        SET claim_id = ?, claimed_at = ?
      WHERE receipt_id IN (
        SELECT receipt_id
          FROM feedback_email_queue
         WHERE claim_id IS NULL
         ORDER BY received_at ASC, receipt_id ASC
         LIMIT ?
      )
      RETURNING receipt_id, received_at, app_version, category, message,
                contact_email, screenshot_mime_type`,
  ).bind(claimId, nowSeconds, batchSize).all<PendingFeedback>();
  const reports = claimed.results;
  if (reports.length === 0) {
    // Another dispatch claimed the backlog between the count and the claim.
    // The reserved slot has to go back, or a race silently costs the day a send.
    await releaseReservation(db, notificationDay, nowSeconds);
    return { ...empty, pending: 0 };
  }

  let messageId: string | null = null;
  try {
    const sendResult = await configuration.email.send({
      from: configuration.from,
      to: configuration.to,
      subject: feedbackEmailSubject(reports),
      text: feedbackEmailText(reports, configuration.dashboardUrl, notificationDay,
                              reservation.sent_count, dailyLimit),
    });
    messageId = sendResult?.messageId ?? null;
  } catch (error) {
    await releaseClaim(db, claimId);
    await releaseReservation(db, notificationDay, nowSeconds);
    console.error("Unable to send feedback email batch", error);
    return {
      ...empty,
      outcome: "send_failed",
      pending: pendingCount,
      remainingDailyEmails: dailyLimit - reservation.sent_count + 1,
      error: error instanceof Error ? error.message : String(error),
    };
  }

  await db.prepare(
    "DELETE FROM feedback_email_queue WHERE claim_id = ?",
  ).bind(claimId).run();
  return {
    outcome: "sent",
    sent: reports.length,
    pending: pendingCount - reports.length,
    dailyLimit,
    dailyEmailNumber: reservation.sent_count,
    remainingDailyEmails: dailyLimit - reservation.sent_count,
    messageId,
    problems: [],
    error: null,
  };
}

export async function notificationStatus(
  db: D1Database,
  env: NotificationEnv,
  now = new Date(),
): Promise<NotificationStatus> {
  const { configuration, problems } = notificationConfiguration(env);
  const dailyLimit = dailyEmailLimit(env);
  const notificationDay = now.toISOString().slice(0, 10);

  const [queue, day, attempt] = await Promise.all([
    db.prepare(
      `SELECT COUNT(*) AS total,
              COALESCE(SUM(CASE WHEN claim_id IS NULL THEN 1 ELSE 0 END), 0) AS pending,
              MIN(received_at) AS oldest_received_at
         FROM feedback_email_queue`,
    ).first<{ total: number; pending: number; oldest_received_at: number | null }>(),
    db.prepare(
      "SELECT sent_count FROM feedback_notification_days WHERE notification_day = ?",
    ).bind(notificationDay).first<{ sent_count: number }>(),
    db.prepare(
      `SELECT actor, completed_at, details_json FROM maintenance_runs
        WHERE operation = 'notification' ORDER BY completed_at DESC, id DESC LIMIT 1`,
    ).first<{ actor: string; completed_at: number; details_json: string }>(),
  ]);

  const total = Number(queue?.total ?? 0);
  const pending = Number(queue?.pending ?? 0);
  const sentToday = Number(day?.sent_count ?? 0);

  return {
    configured: configuration !== null,
    problems,
    from: boundedValue(env.FEEDBACK_NOTIFICATION_FROM, maximumEmailAddressLength),
    to: boundedValue(env.FEEDBACK_NOTIFICATION_TO, maximumEmailAddressLength),
    dashboardUrl: boundedValue(env.FEEDBACK_DASHBOARD_URL, maximumDashboardUrlLength),
    queue: {
      pending,
      claimed: total - pending,
      oldestReceivedAt: queue?.oldest_received_at
        ? new Date(Number(queue.oldest_received_at) * 1000).toISOString()
        : null,
    },
    daily: {
      day: notificationDay,
      sent: sentToday,
      limit: dailyLimit,
      remaining: Math.max(0, dailyLimit - sentToday),
    },
    lastAttempt: presentAttempt(attempt),
  };
}

function presentAttempt(row: {
  actor: string;
  completed_at: number;
  details_json: string;
} | null): DispatchAttempt | null {
  if (!row) return null;
  let details: Partial<DispatchResult> = {};
  try {
    details = JSON.parse(row.details_json) as Partial<DispatchResult>;
  } catch {
    details = {};
  }
  return {
    actor: row.actor,
    outcome: details.outcome ?? "nothing_pending",
    completedAt: new Date(row.completed_at * 1000).toISOString(),
    sent: Number(details.sent ?? 0),
    problems: details.problems ?? [],
    error: details.error ?? null,
  };
}

// A dispatch that ran and found nothing and a dispatch that never ran are
// different facts. Recording every attempt, not only the ones that send, is
// what keeps them apart.
async function recordDispatchAttempt(
  db: D1Database,
  actor: string,
  startedAt: number,
  result: DispatchResult,
): Promise<void> {
  try {
    await db.prepare(
      `INSERT INTO maintenance_runs (operation, actor, started_at, completed_at, details_json)
       VALUES ('notification', ?, ?, ?, ?)`,
    ).bind(actor, startedAt, Math.floor(Date.now() / 1000), JSON.stringify(result)).run();
  } catch (error) {
    // The dispatch already happened; failing to write its history must not
    // change what the caller is told about it.
    console.error("Unable to record the feedback dispatch attempt", error);
  }
}

function feedbackEmailSubject(reports: PendingFeedback[]): string {
  if (reports.length === 1) {
    return `Practice Takes: new ${reports[0]?.category ?? "user"} feedback`;
  }
  return `Practice Takes: ${reports.length} new feedback reports`;
}

function feedbackEmailText(
  reports: PendingFeedback[],
  dashboardUrl: string,
  notificationDay: string,
  dailyEmailNumber: number,
  dailyLimit: number,
): string {
  const sections = reports.map((report, index) => [
    `Feedback ${index + 1} of ${reports.length}`,
    `Receipt: ${report.receipt_id}`,
    `Received: ${new Date(report.received_at * 1000).toISOString()}`,
    `Category: ${report.category}`,
    `Application version: ${report.app_version}`,
    `Contact: ${report.contact_email ?? "not provided"}`,
    `Screenshot: ${report.screenshot_mime_type ? "available in the private dashboard" : "none"}`,
    "",
    report.message,
  ].join("\n"));

  return [
    `${reports.length} Practice Takes feedback report${reports.length === 1 ? "" : "s"} received.`,
    `This is email ${dailyEmailNumber} of at most ${dailyLimit} for ${notificationDay} UTC.`,
    `Private dashboard: ${dashboardUrl}`,
    "",
    sections.join("\n\n----------------------------------------\n\n"),
  ].join("\n");
}

async function releaseClaim(db: D1Database, claimId: string): Promise<void> {
  try {
    await db.prepare(
      `UPDATE feedback_email_queue
          SET claim_id = NULL, claimed_at = NULL
        WHERE claim_id = ?`,
    ).bind(claimId).run();
  } catch (error) {
    console.error("Unable to release failed feedback email claim", error);
  }
}

async function releaseReservation(db: D1Database, notificationDay: string,
                                  nowSeconds: number): Promise<void> {
  try {
    await db.prepare(
      `UPDATE feedback_notification_days
          SET sent_count = MAX(sent_count - 1, 0), updated_at = ?
        WHERE notification_day = ?`,
    ).bind(nowSeconds, notificationDay).run();
  } catch (error) {
    console.error("Unable to release the reserved feedback notification slot", error);
  }
}

// Every problem is collected rather than short-circuited at the first: an
// operator repairing configuration one deploy at a time is exactly who reads
// this.
export function notificationConfiguration(env: NotificationEnv): ConfigurationResult {
  const problems: string[] = [];
  const from = env.FEEDBACK_NOTIFICATION_FROM?.trim().toLowerCase() ?? "";
  const to = env.FEEDBACK_NOTIFICATION_TO?.trim().toLowerCase() ?? "";
  const dashboardUrl = env.FEEDBACK_DASHBOARD_URL?.trim() ?? "";
  const administrators = (env.ADMIN_EMAILS ?? "")
    .split(",")
    .map((email) => email.trim().toLowerCase())
    .filter(Boolean);

  if (!env.FEEDBACK_EMAIL) problems.push("missing_email_binding");

  if (from.length === 0) {
    problems.push("missing_from_address");
  } else if (!isEmailAddress(from)) {
    problems.push("invalid_from_address");
  } else if (!isSendableDomain(from)) {
    problems.push("sender_domain_not_sendable");
  }

  if (to.length === 0) {
    problems.push("missing_to_address");
  } else if (!isEmailAddress(to)) {
    problems.push("invalid_to_address");
  }

  if (administrators.length === 0) {
    problems.push("administrator_not_configured");
  } else if (administrators.length > 1) {
    problems.push("multiple_administrators");
  } else if (to.length > 0 && administrators[0] !== to) {
    problems.push("recipient_not_administrator");
  }

  const url = parsedDashboardUrl(dashboardUrl);
  if (dashboardUrl.length === 0) {
    problems.push("missing_dashboard_url");
  } else if (!url) {
    problems.push("invalid_dashboard_url");
  }

  if (problems.length > 0 || !env.FEEDBACK_EMAIL || !url) {
    return { configuration: null, problems };
  }
  return {
    configuration: { email: env.FEEDBACK_EMAIL, from, to, dashboardUrl: url.href },
    problems: [],
  };
}

function parsedDashboardUrl(value: string): URL | null {
  if (value.length === 0 || value.length > maximumDashboardUrlLength) return null;
  try {
    const url = new URL(value);
    if (
      url.protocol !== "https:" ||
      url.username ||
      url.password ||
      url.port ||
      url.pathname !== "/admin" ||
      url.search ||
      url.hash
    ) {
      return null;
    }
    return url;
  } catch {
    return null;
  }
}

function isSendableDomain(address: string): boolean {
  const domain = address.slice(address.lastIndexOf("@") + 1);
  return !unsendableDomains.has(domain) &&
    !unsendableSuffixes.some((suffix) => domain.endsWith(suffix));
}

function boundedValue(value: string | undefined, limit: number): string | null {
  const trimmed = value?.trim() ?? "";
  return trimmed.length === 0 ? null : trimmed.slice(0, limit);
}

function isEmailAddress(value: string): boolean {
  if (value.length === 0 || value.length > maximumEmailAddressLength) {
    return false;
  }

  let atIndex = -1;
  for (let index = 0; index < value.length; index += 1) {
    const character = value[index] ?? "";
    if (character.trim() === "") return false;
    if (character === "@") {
      if (atIndex !== -1) return false;
      atIndex = index;
    }
  }

  if (atIndex <= 0 || atIndex >= value.length - 1) return false;
  const firstDomainDot = value.indexOf(".", atIndex + 1);
  return firstDomainDot > atIndex + 1 && firstDomainDot < value.length - 1;
}
