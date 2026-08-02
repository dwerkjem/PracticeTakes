import { describe, expect, it, vi } from "vitest";

import { sendPendingFeedbackBatch } from "../src/notifications";
import {
  asD1,
  createTestDatabase,
  seedQueuedFeedback,
  type TestDatabase,
} from "./support/database";

function queued(database: TestDatabase): Array<{
  receipt_id: string;
  claim_id: string | null;
  claimed_at: number | null;
}> {
  return database.rows(
    "SELECT receipt_id, claim_id, claimed_at FROM feedback_email_queue ORDER BY received_at ASC",
  );
}

function dailyCount(database: TestDatabase, day: string): number | undefined {
  return database.row<{ sent_count: number }>(
    "SELECT sent_count FROM feedback_notification_days WHERE notification_day = ?",
    day,
  )?.sent_count;
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
  it.each([
    "FEEDBACK_NOTIFICATION_FROM",
    "FEEDBACK_NOTIFICATION_TO",
  ] as const)("rejects oversized adversarial %s values before querying the queue",
              async (field) => {
                const database = createTestDatabase();
                seedQueuedFeedback(database, "adversarial-address", 1);
                const send = vi.fn(async (_message: unknown) => undefined);
                const env = {
                  ...environment(send),
                  [field]: `!@!.${"!.".repeat(10_000)}`,
                };

                expect(await sendPendingFeedbackBatch(
                  asD1(database),
                  env,
                  new Date("2026-07-24T03:17:00Z"),
                )).toBe(0);
                expect(send).not.toHaveBeenCalled();
                expect(queued(database)).toHaveLength(1);
              });

  it("fails closed when the email recipient is not the one Access administrator", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "private-recipient", 1);
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = {
      ...environment(send),
      FEEDBACK_NOTIFICATION_TO: "other@example.com",
    };

    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-24T03:17:00Z"),
    )).toBe(0);
    expect(send).not.toHaveBeenCalled();
    expect(queued(database)).toHaveLength(1);
  });

  it("emails every queued report in the smallest balanced batches with three daily emails",
     async () => {
       const database = createTestDatabase();
       for (let index = 1; index <= 5; index += 1) {
         seedQueuedFeedback(database, `receipt-${index}`, index);
       }
       const send = vi.fn(async (_message: unknown) => undefined);
       const env = environment(send);

       expect(await sendPendingFeedbackBatch(
         asD1(database), env, new Date("2026-07-24T03:17:00Z"),
       )).toBe(2);
       expect(await sendPendingFeedbackBatch(
         asD1(database), env, new Date("2026-07-24T11:17:00Z"),
       )).toBe(2);
       expect(await sendPendingFeedbackBatch(
         asD1(database), env, new Date("2026-07-24T19:17:00Z"),
       )).toBe(1);

       expect(send).toHaveBeenCalledTimes(3);
       expect(queued(database)).toHaveLength(0);
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
    const database = createTestDatabase();
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = environment(send);

    for (let index = 1; index <= 3; index += 1) {
      seedQueuedFeedback(database, `day-one-${index}`, index);
      expect(await sendPendingFeedbackBatch(
        asD1(database),
        env,
        new Date(`2026-07-24T${String(index * 3).padStart(2, "0")}:17:00Z`),
      )).toBe(1);
    }
    seedQueuedFeedback(database, "after-cap-1", 10);
    seedQueuedFeedback(database, "after-cap-2", 11);

    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-24T20:00:00Z"),
    )).toBe(0);
    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-25T03:17:00Z"),
    )).toBe(1);
    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-25T11:17:00Z"),
    )).toBe(1);
    expect(send).toHaveBeenCalledTimes(5);
    expect(queued(database)).toHaveLength(0);
  });

  it("keeps oversized daily backlogs queued instead of creating an oversized email", async () => {
    const database = createTestDatabase();
    for (let index = 1; index <= 305; index += 1) {
      seedQueuedFeedback(database, `large-${index}`, index);
    }
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = environment(send);

    for (const hour of ["03", "11", "19"]) {
      expect(await sendPendingFeedbackBatch(
        asD1(database),
        env,
        new Date(`2026-07-24T${hour}:17:00Z`),
      )).toBe(100);
    }

    expect(send).toHaveBeenCalledTimes(3);
    expect(queued(database)).toHaveLength(5);
    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-25T03:17:00Z"),
    )).toBe(2);
  });

  it("returns failed batches to the durable queue for a later attempt", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "retry-me", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined)
      .mockRejectedValueOnce(new Error("mail service unavailable"))
      .mockResolvedValue(undefined);
    const env = environment(send);

    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-24T03:17:00Z"),
    )).toBe(0);
    expect(queued(database)[0]?.claim_id).toBeNull();
    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-24T11:17:00Z"),
    )).toBe(1);
    expect(queued(database)).toHaveLength(0);
    expect(errorLog).toHaveBeenCalledOnce();
    errorLog.mockRestore();
  });

  it("returns the reserved daily slot when a send fails", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "undelivered", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined)
      .mockRejectedValueOnce(new Error("mail service unavailable"))
      .mockRejectedValueOnce(new Error("mail service unavailable"))
      .mockRejectedValueOnce(new Error("mail service unavailable"))
      .mockResolvedValue(undefined);
    const env = environment(send);

    for (const hour of ["03", "07", "11"]) {
      expect(await sendPendingFeedbackBatch(
        asD1(database), env, new Date(`2026-07-24T${hour}:17:00Z`),
      )).toBe(0);
    }

    expect(await sendPendingFeedbackBatch(
      asD1(database), env, new Date("2026-07-24T15:17:00Z"),
    )).toBe(1);
    expect(dailyCount(database, "2026-07-24")).toBe(1);
    expect(queued(database)).toHaveLength(0);
    errorLog.mockRestore();
  });

  it("releases claims that outlive their processing window", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "stale-claim", 1);
    database.execute(
      "UPDATE feedback_email_queue SET claim_id = 'abandoned', claimed_at = ? WHERE receipt_id = ?",
      Math.floor(Date.parse("2026-07-24T02:00:00Z") / 1000),
      "stale-claim",
    );
    const send = vi.fn(async (_message: unknown) => undefined);

    expect(await sendPendingFeedbackBatch(
      asD1(database), environment(send), new Date("2026-07-24T03:17:00Z"),
    )).toBe(1);
    expect(queued(database)).toHaveLength(0);
  });
});
