import { describe, expect, it, vi } from "vitest";

import { sendPendingFeedbackBatch } from "../src/notifications";

interface QueuedFeedback {
  receipt_id: string;
  received_at: number;
  app_version: string;
  category: string;
  message: string;
  contact_email: string | null;
  screenshot_mime_type: string | null;
  claim_id: string | null;
  claimed_at: number | null;
}

class NotificationStatement {
  constructor(
    private readonly database: NotificationDatabase,
    private readonly sql: string,
    private readonly parameters: unknown[] = [],
  ) {}

  bind(...parameters: unknown[]): NotificationStatement {
    return new NotificationStatement(this.database, this.sql, parameters);
  }

  async first<T>(): Promise<T | null> {
    if (this.sql.includes("SELECT COUNT(*) AS count")) {
      return {
        count: this.database.reports.filter(
          (report) => report.claim_id === null,
        ).length,
      } as T;
    }
    if (this.sql.includes("feedback_notification_days")) {
      const day = this.parameters[0] as string;
      const count = this.database.dailyAttempts.get(day) ?? 0;
      if (count >= 3) return null;
      this.database.dailyAttempts.set(day, count + 1);
      return { sent_count: count + 1 } as T;
    }
    return null;
  }

  async all<T>(): Promise<D1Result<T>> {
    if (!this.sql.includes("RETURNING receipt_id")) {
      return { success: true, results: [], meta: {} } as unknown as D1Result<T>;
    }
    const [claimId, claimedAt, limit] = this.parameters as [string, number, number];
    const reports = this.database.reports
      .filter((report) => report.claim_id === null)
      .sort((left, right) =>
        left.received_at - right.received_at || left.receipt_id.localeCompare(right.receipt_id),
      )
      .slice(0, limit);
    for (const report of reports) {
      report.claim_id = claimId;
      report.claimed_at = claimedAt;
    }
    return { success: true, results: reports as T[], meta: {} } as D1Result<T>;
  }

  async run(): Promise<D1Result> {
    if (this.sql.includes("claimed_at < ?")) {
      const cutoff = this.parameters[0] as number;
      for (const report of this.database.reports) {
        if (
          report.claim_id !== null &&
          (report.claimed_at ?? 0) < cutoff
        ) {
          report.claim_id = null;
          report.claimed_at = null;
        }
      }
    } else if (this.sql.includes("DELETE FROM feedback_email_queue")) {
      const claimId = this.parameters[0] as string;
      for (let index = this.database.reports.length - 1; index >= 0; index -= 1) {
        if (this.database.reports[index]?.claim_id === claimId) {
          this.database.reports.splice(index, 1);
        }
      }
    } else if (this.sql.includes("WHERE claim_id = ?")) {
      const claimId = this.parameters[0] as string;
      for (const report of this.database.reports.filter(
        (candidate) => candidate.claim_id === claimId,
      )) {
        report.claim_id = null;
        report.claimed_at = null;
      }
    }
    return { success: true, meta: {} } as D1Result;
  }
}

class NotificationDatabase {
  readonly reports: QueuedFeedback[] = [];
  readonly dailyAttempts = new Map<string, number>();

  prepare(sql: string): D1PreparedStatement {
    return new NotificationStatement(this, sql) as unknown as D1PreparedStatement;
  }

  add(receipt: string, receivedAt: number): void {
    this.reports.push({
      receipt_id: receipt,
      received_at: receivedAt,
      app_version: "0.4.3",
      category: "bug",
      message: `Complete feedback for ${receipt}`,
      contact_email: `${receipt}@example.com`,
      screenshot_mime_type: null,
      claim_id: null,
      claimed_at: null,
    });
  }
}

function environment(send: (message: unknown) => Promise<void>) {
  return {
    FEEDBACK_EMAIL: { send: send as unknown as SendEmail["send"] },
    FEEDBACK_NOTIFICATION_FROM: "feedback@practicetakes.app",
    FEEDBACK_NOTIFICATION_TO: "developer@example.com",
    FEEDBACK_DASHBOARD_URL: "https://feedback.example.test/admin",
    ADMIN_EMAILS: "developer@example.com",
  };
}

describe("feedback email batching", () => {
  it("fails closed when the email recipient is not the one Access administrator", async () => {
    const database = new NotificationDatabase();
    database.add("private-recipient", 1);
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = {
      ...environment(send),
      FEEDBACK_NOTIFICATION_TO: "other@example.com",
    };

    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-24T03:17:00Z"),
    )).toBe(0);
    expect(send).not.toHaveBeenCalled();
    expect(database.reports).toHaveLength(1);
  });

  it("emails every queued report in the smallest balanced batches with three daily emails",
     async () => {
       const database = new NotificationDatabase();
       for (let index = 1; index <= 5; index += 1) database.add(`receipt-${index}`, index);
       const send = vi.fn(async (_message: unknown) => undefined);
       const env = environment(send);

       expect(await sendPendingFeedbackBatch(
         database as unknown as D1Database, env, new Date("2026-07-24T03:17:00Z"),
       )).toBe(2);
       expect(await sendPendingFeedbackBatch(
         database as unknown as D1Database, env, new Date("2026-07-24T11:17:00Z"),
       )).toBe(2);
       expect(await sendPendingFeedbackBatch(
         database as unknown as D1Database, env, new Date("2026-07-24T19:17:00Z"),
       )).toBe(1);

       expect(send).toHaveBeenCalledTimes(3);
       expect(database.reports).toHaveLength(0);
       expect(send.mock.calls.map(([message]) =>
         (message as { text: string }).text.match(/Feedback \d+ of \d+/g)?.length,
       )).toEqual([2, 2, 1]);
       const combinedEmail = send.mock.calls
         .map(([message]) => (message as { text: string }).text)
         .join("\n");
       for (let index = 1; index <= 5; index += 1) {
         expect(combinedEmail).toContain(`Complete feedback for receipt-${index}`);
       }
     });

  it("queues reports received after the third email for the next UTC day", async () => {
    const database = new NotificationDatabase();
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = environment(send);

    for (let index = 1; index <= 3; index += 1) {
      database.add(`day-one-${index}`, index);
      expect(await sendPendingFeedbackBatch(
        database as unknown as D1Database,
        env,
        new Date(`2026-07-24T${String(index * 3).padStart(2, "0")}:17:00Z`),
      )).toBe(1);
    }
    database.add("after-cap-1", 10);
    database.add("after-cap-2", 11);

    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-24T20:00:00Z"),
    )).toBe(0);
    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-25T03:17:00Z"),
    )).toBe(1);
    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-25T11:17:00Z"),
    )).toBe(1);
    expect(send).toHaveBeenCalledTimes(5);
    expect(database.reports).toHaveLength(0);
  });

  it("keeps oversized daily backlogs queued instead of creating an oversized email", async () => {
    const database = new NotificationDatabase();
    for (let index = 1; index <= 305; index += 1) database.add(`large-${index}`, index);
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = environment(send);

    for (const hour of ["03", "11", "19"]) {
      expect(await sendPendingFeedbackBatch(
        database as unknown as D1Database,
        env,
        new Date(`2026-07-24T${hour}:17:00Z`),
      )).toBe(100);
    }

    expect(send).toHaveBeenCalledTimes(3);
    expect(database.reports).toHaveLength(5);
    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-25T03:17:00Z"),
    )).toBe(2);
  });

  it("returns failed batches to the durable queue for a later attempt", async () => {
    const database = new NotificationDatabase();
    database.add("retry-me", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined)
      .mockRejectedValueOnce(new Error("mail service unavailable"))
      .mockResolvedValue(undefined);
    const env = environment(send);

    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-24T03:17:00Z"),
    )).toBe(0);
    expect(database.reports[0]?.claim_id).toBeNull();
    expect(await sendPendingFeedbackBatch(
      database as unknown as D1Database, env, new Date("2026-07-24T11:17:00Z"),
    )).toBe(1);
    expect(database.reports).toHaveLength(0);
    expect(errorLog).toHaveBeenCalledOnce();
    errorLog.mockRestore();
  });
});
