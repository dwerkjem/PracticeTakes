export interface NotificationEnv {
  FEEDBACK_EMAIL?: SendEmail;
  FEEDBACK_NOTIFICATION_FROM?: string;
  FEEDBACK_NOTIFICATION_TO?: string;
  FEEDBACK_DASHBOARD_URL?: string;
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

const maximumDailyEmails = 3;
// Feedback messages are limited to 8,000 UTF-16 code units. Capping a batch
// at 100 keeps even worst-case UTF-8 content safely below Cloudflare Email
// Service's 5 MiB total-message limit after headers and formatting.
const maximumFeedbackPerEmail = 100;
const staleClaimSeconds = 30 * 60;
const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export async function sendPendingFeedbackBatch(
  db: D1Database,
  env: NotificationEnv,
  now = new Date(),
): Promise<number> {
  const configuration = notificationConfiguration(env);
  if (!configuration) return 0;

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
  if (pendingCount < 1) return 0;

  const notificationDay = now.toISOString().slice(0, 10);
  const reservation = await db.prepare(
    `INSERT INTO feedback_notification_days (notification_day, sent_count, updated_at)
     VALUES (?, 1, ?)
     ON CONFLICT(notification_day) DO UPDATE SET
       sent_count = feedback_notification_days.sent_count + 1,
       updated_at = excluded.updated_at
     WHERE feedback_notification_days.sent_count < ?
     RETURNING sent_count`,
  ).bind(notificationDay, nowSeconds, maximumDailyEmails)
    .first<{ sent_count: number }>();
  if (!reservation) return 0;

  const remainingSlots = maximumDailyEmails - reservation.sent_count + 1;
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
  if (reports.length === 0) return 0;

  try {
    await configuration.email.send({
      from: configuration.from,
      to: configuration.to,
      subject: feedbackEmailSubject(reports),
      text: feedbackEmailText(reports, configuration.dashboardUrl, notificationDay,
                              reservation.sent_count),
    });
  } catch (error) {
    await releaseClaim(db, claimId);
    console.error("Unable to send feedback email batch", error);
    return 0;
  }

  await db.prepare(
    "DELETE FROM feedback_email_queue WHERE claim_id = ?",
  ).bind(claimId).run();
  return reports.length;
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
    `This is email ${dailyEmailNumber} of at most ${maximumDailyEmails} for ${notificationDay} UTC.`,
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

function notificationConfiguration(env: NotificationEnv): {
  email: SendEmail;
  from: string;
  to: string;
  dashboardUrl: string;
} | null {
  const from = env.FEEDBACK_NOTIFICATION_FROM?.trim().toLowerCase() ?? "";
  const to = env.FEEDBACK_NOTIFICATION_TO?.trim().toLowerCase() ?? "";
  const dashboardUrl = env.FEEDBACK_DASHBOARD_URL?.trim() ?? "";
  const administrators = (env.ADMIN_EMAILS ?? "")
    .split(",")
    .map((email) => email.trim().toLowerCase())
    .filter(Boolean);
  if (
    !env.FEEDBACK_EMAIL ||
    !emailPattern.test(from) ||
    !emailPattern.test(to) ||
    administrators.length !== 1 ||
    administrators[0] !== to
  ) {
    return null;
  }

  try {
    const url = new URL(dashboardUrl);
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
    return { email: env.FEEDBACK_EMAIL, from, to, dashboardUrl: url.href };
  } catch {
    return null;
  }
}
