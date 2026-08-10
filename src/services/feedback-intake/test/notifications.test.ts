import { describe, expect, it, vi } from "vitest";

import {
  dailyEmailLimit,
  notificationConfiguration,
  notificationStatus,
  sendPendingFeedbackBatch,
  type NotificationEnv,
} from "../src/notifications";
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

function attempts(database: TestDatabase): Array<{
  actor: string;
  details_json: string;
}> {
  return database.rows(
    `SELECT actor, details_json FROM maintenance_runs
      WHERE operation = 'notification' ORDER BY id ASC`,
  );
}

function outcomes(database: TestDatabase): string[] {
  return attempts(database).map(
    (row) => (JSON.parse(row.details_json) as { outcome: string }).outcome,
  );
}

function environment(send: (message: unknown) => Promise<unknown>) {
  return {
    FEEDBACK_EMAIL: { send: send as unknown as SendEmail["send"] },
    FEEDBACK_NOTIFICATION_FROM: "feedback@practicetakes.app",
    FEEDBACK_NOTIFICATION_TO: "developer@example.com",
    FEEDBACK_DASHBOARD_URL: "https://feedback.example.test/admin",
    ADMIN_EMAILS: "developer@example.com",
  };
}

const dispatch = (
  database: TestDatabase,
  env: NotificationEnv,
  at: string,
  actor = "scheduled",
) => sendPendingFeedbackBatch(asD1(database), env, actor, new Date(at));

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

                const result = await dispatch(database, env, "2026-07-24T03:17:00Z");
                expect(result.outcome).toBe("not_configured");
                expect(result.sent).toBe(0);
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

    const result = await dispatch(database, env, "2026-07-24T03:17:00Z");
    expect(result.outcome).toBe("not_configured");
    expect(result.problems).toContain("recipient_not_administrator");
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

       expect((await dispatch(database, env, "2026-07-24T03:17:00Z")).sent).toBe(2);
       expect((await dispatch(database, env, "2026-07-24T11:17:00Z")).sent).toBe(2);
       expect((await dispatch(database, env, "2026-07-24T19:17:00Z")).sent).toBe(1);

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
      const hour = String(index * 3).padStart(2, "0");
      expect((await dispatch(database, env, `2026-07-24T${hour}:17:00Z`)).sent).toBe(1);
    }
    seedQueuedFeedback(database, "after-cap-1", 10);
    seedQueuedFeedback(database, "after-cap-2", 11);

    const capped = await dispatch(database, env, "2026-07-24T20:00:00Z");
    expect(capped.outcome).toBe("daily_limit_reached");
    expect(capped.remainingDailyEmails).toBe(0);
    expect((await dispatch(database, env, "2026-07-25T03:17:00Z")).sent).toBe(1);
    expect((await dispatch(database, env, "2026-07-25T11:17:00Z")).sent).toBe(1);
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
      expect((await dispatch(database, env, `2026-07-24T${hour}:17:00Z`)).sent).toBe(100);
    }

    expect(send).toHaveBeenCalledTimes(3);
    expect(queued(database)).toHaveLength(5);
    expect((await dispatch(database, env, "2026-07-25T03:17:00Z")).sent).toBe(2);
  });

  it("returns failed batches to the durable queue for a later attempt", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "retry-me", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined)
      .mockRejectedValueOnce(new Error("mail service unavailable"))
      .mockResolvedValue(undefined);
    const env = environment(send);

    const failed = await dispatch(database, env, "2026-07-24T03:17:00Z");
    expect(failed.outcome).toBe("send_failed");
    expect(failed.sent).toBe(0);
    expect(failed.error).toBe("mail service unavailable");
    expect(queued(database)[0]?.claim_id).toBeNull();
    expect((await dispatch(database, env, "2026-07-24T11:17:00Z")).sent).toBe(1);
    expect(queued(database)).toHaveLength(0);
    expect(errorLog).toHaveBeenCalledOnce();
    errorLog.mockRestore();
  });

  it("reports the provider's reason without its internals", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "leaky", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined).mockRejectedValueOnce(
      new Error(
        "550 5.7.1 domain not verified\n" +
        "    at send (/opt/worker/src/notifications.ts:214:31)\n" +
        "    at dispatch (/opt/worker/src/notifications.ts:118:18)",
      ),
    );

    const result = await dispatch(database, environment(send), "2026-07-24T03:17:00Z");

    expect(result.outcome).toBe("send_failed");
    expect(result.error).toBe("550 5.7.1 domain not verified");
    expect(result.error).not.toContain("notifications.ts");
    expect(result.error).not.toContain("at send");
    // The whole object still reaches the log, where the detail belongs.
    expect(errorLog).toHaveBeenCalledOnce();
    errorLog.mockRestore();
  });

  it("bounds a hostile provider message", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "verbose", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined)
      .mockRejectedValueOnce(new Error("x".repeat(10_000)));

    const result = await dispatch(database, environment(send), "2026-07-24T03:17:00Z");

    expect(result.error).toHaveLength(200);
    errorLog.mockRestore();
  });

  it("reports a thrown non-Error without crashing", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "not-an-error", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined)
      .mockRejectedValueOnce("plain string rejection");

    const result = await dispatch(database, environment(send), "2026-07-24T03:17:00Z");

    expect(result.outcome).toBe("send_failed");
    expect(result.error).toBe("plain string rejection");
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
      expect((await dispatch(database, env, `2026-07-24T${hour}:17:00Z`)).outcome)
        .toBe("send_failed");
    }

    expect((await dispatch(database, env, "2026-07-24T15:17:00Z")).sent).toBe(1);
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

    expect((await dispatch(database, environment(send), "2026-07-24T03:17:00Z")).sent).toBe(1);
    expect(queued(database)).toHaveLength(0);
  });
});

describe("dispatch outcomes", () => {
  it("separates an empty queue from a delivery path that cannot deliver", async () => {
    const database = createTestDatabase();
    const send = vi.fn(async (_message: unknown) => undefined);

    const empty = await dispatch(database, environment(send), "2026-07-24T03:17:00Z");
    expect(empty).toMatchObject({ outcome: "nothing_pending", sent: 0, pending: 0 });

    seedQueuedFeedback(database, "unreachable", 1);
    const broken = await dispatch(
      database,
      { ...environment(send), FEEDBACK_NOTIFICATION_FROM: "feedback@example.com" },
      "2026-07-24T11:17:00Z",
    );
    expect(broken.outcome).toBe("not_configured");
    expect(broken.problems).toEqual(["sender_domain_not_sendable"]);
    expect(send).not.toHaveBeenCalled();
  });

  it("reports the provider message identifier for a delivered batch", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "identified", 1);
    const send = vi.fn(async (_message: unknown) => ({ messageId: "cf-message-1" }));

    const result = await dispatch(database, environment(send), "2026-07-24T03:17:00Z");
    expect(result).toMatchObject({
      outcome: "sent",
      sent: 1,
      pending: 0,
      messageId: "cf-message-1",
      dailyEmailNumber: 1,
      dailyLimit: 3,
      remainingDailyEmails: 2,
    });
  });
});

describe("delivery configuration problems", () => {
  it("reports every problem it finds rather than stopping at the first", () => {
    const { configuration, problems } = notificationConfiguration({
      FEEDBACK_NOTIFICATION_FROM: "not-an-address",
      FEEDBACK_NOTIFICATION_TO: "also-not-an-address",
      FEEDBACK_DASHBOARD_URL: "http://feedback.example.test/admin",
      ADMIN_EMAILS: "",
    });

    expect(configuration).toBeNull();
    expect(problems).toEqual([
      "missing_email_binding",
      "invalid_from_address",
      "invalid_to_address",
      "administrator_not_configured",
      "invalid_dashboard_url",
    ]);
  });

  it("names each missing setting separately from an invalid one", () => {
    const { problems } = notificationConfiguration({
      FEEDBACK_EMAIL: { send: (async () => undefined) as unknown as SendEmail["send"] },
    });

    expect(problems).toEqual([
      "missing_from_address",
      "missing_to_address",
      "administrator_not_configured",
      "missing_dashboard_url",
    ]);
  });

  it("rejects more than one administrator", () => {
    const send = vi.fn(async (_message: unknown) => undefined);
    const { problems } = notificationConfiguration({
      ...environment(send),
      ADMIN_EMAILS: "developer@example.com,second@example.com",
    });

    expect(problems).toEqual(["multiple_administrators"]);
  });

  it.each([
    "feedback@example.com",
    "feedback@example.org",
    "feedback@mail.example.com",
    "feedback@mail.invalid",
    "feedback@practicetakes.test",
    "feedback@mail.local",
    "feedback@practice-takes-feedback-intake.derekrneilson.workers.dev",
  ])("refuses %s as a sender that can never deliver", (from) => {
    const send = vi.fn(async (_message: unknown) => undefined);
    const { configuration, problems } = notificationConfiguration({
      ...environment(send),
      FEEDBACK_NOTIFICATION_FROM: from,
    });

    expect(configuration).toBeNull();
    expect(problems).toEqual(["sender_domain_not_sendable"]);
  });

  it("accepts a sender on a domain that could be onboarded", () => {
    const send = vi.fn(async (_message: unknown) => undefined);
    const { configuration, problems } = notificationConfiguration(environment(send));

    expect(problems).toEqual([]);
    expect(configuration).toMatchObject({
      from: "feedback@practicetakes.app",
      to: "developer@example.com",
      dashboardUrl: "https://feedback.example.test/admin",
    });
  });
});

describe("the configured daily email limit", () => {
  it.each([undefined, "", "three", "0", "-1"])(
    "falls back to three for %o", (value) => {
      expect(dailyEmailLimit({ FEEDBACK_MAX_DAILY_EMAILS: value })).toBe(3);
    });

  it("sends a fourth batch in one UTC day when the limit allows it", async () => {
    const database = createTestDatabase();
    for (let index = 1; index <= 4; index += 1) {
      seedQueuedFeedback(database, `over-three-${index}`, index);
    }
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = { ...environment(send), FEEDBACK_MAX_DAILY_EMAILS: "4" };

    for (const hour of ["03", "09", "15", "21"]) {
      const result = await dispatch(database, env, `2026-07-24T${hour}:17:00Z`);
      expect(result.outcome).toBe("sent");
      expect(result.dailyLimit).toBe(4);
    }

    expect(send).toHaveBeenCalledTimes(4);
    expect(dailyCount(database, "2026-07-24")).toBe(4);
    expect(queued(database)).toHaveLength(0);
  });

  it("stops at the configured limit when it is lower than three", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "only-one-1", 1);
    seedQueuedFeedback(database, "only-one-2", 2);
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = { ...environment(send), FEEDBACK_MAX_DAILY_EMAILS: "1" };

    expect((await dispatch(database, env, "2026-07-24T03:17:00Z")).sent).toBe(2);
    seedQueuedFeedback(database, "only-one-3", 3);
    expect((await dispatch(database, env, "2026-07-24T11:17:00Z")).outcome)
      .toBe("daily_limit_reached");
    expect(send).toHaveBeenCalledOnce();
  });

  it("tells the reader which email of how many they are holding", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "numbered", 1);
    const send = vi.fn(async (_message: unknown) => undefined);

    await dispatch(database, { ...environment(send), FEEDBACK_MAX_DAILY_EMAILS: "5" },
                   "2026-07-24T03:17:00Z");

    expect((send.mock.calls[0]?.[0] as { text: string }).text)
      .toContain("This is email 1 of at most 5 for 2026-07-24 UTC.");
  });
});

describe("dispatch history", () => {
  it("records attempts that send nothing, against the actor that caused them", async () => {
    const database = createTestDatabase();
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = environment(send);

    await dispatch(database, env, "2026-07-24T03:17:00Z");
    seedQueuedFeedback(database, "recorded", 1);
    await dispatch(database, env, "2026-07-24T11:17:00Z", "developer@example.com");

    expect(attempts(database).map((row) => row.actor))
      .toEqual(["scheduled", "developer@example.com"]);
    expect(outcomes(database)).toEqual(["nothing_pending", "sent"]);
  });

  it("records why a dispatch could not be delivered", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "undeliverable", 1);
    const send = vi.fn(async (_message: unknown) => undefined);

    await dispatch(
      database,
      { ...environment(send), FEEDBACK_NOTIFICATION_FROM: "feedback@example.com" },
      "2026-07-24T03:17:00Z",
    );

    const details = JSON.parse(attempts(database)[0]?.details_json ?? "{}") as {
      outcome: string;
      problems: string[];
    };
    expect(details.outcome).toBe("not_configured");
    expect(details.problems).toEqual(["sender_domain_not_sendable"]);
  });

  it("keeps the dispatch outcome when the attempt cannot be recorded", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "unrecorded", 1);
    const errorLog = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const send = vi.fn(async (_message: unknown) => undefined);
    database.execute("DROP TABLE maintenance_runs");

    const result = await dispatch(database, environment(send), "2026-07-24T03:17:00Z");

    expect(result.outcome).toBe("sent");
    expect(result.sent).toBe(1);
    expect(queued(database)).toHaveLength(0);
    expect(errorLog).toHaveBeenCalledOnce();
    errorLog.mockRestore();
  });
});

describe("delivery status", () => {
  it("reports the queue, the day's counters, and the last attempt", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "status-1", 1_756_000_000);
    seedQueuedFeedback(database, "status-2", 1_756_000_100);
    const send = vi.fn(async (_message: unknown) => undefined);
    const env = { ...environment(send), FEEDBACK_MAX_DAILY_EMAILS: "4" };

    await dispatch(database, env, "2026-07-24T03:17:00Z", "developer@example.com");
    seedQueuedFeedback(database, "status-3", 1_756_000_200);
    const status = await notificationStatus(asD1(database), env,
                                            new Date("2026-07-24T04:00:00Z"));

    expect(status).toMatchObject({
      configured: true,
      problems: [],
      from: "feedback@practicetakes.app",
      to: "developer@example.com",
      // Two queued reports spread across four remaining sends is one per
      // email, so one stays behind and `status-3` joins it.
      queue: { pending: 2, claimed: 0 },
      daily: { day: "2026-07-24", sent: 1, limit: 4, remaining: 3 },
    });
    expect(status.lastAttempt).toMatchObject({
      actor: "developer@example.com",
      outcome: "sent",
      sent: 1,
    });
  });

  it("reports the problems and the offending value when delivery is broken", async () => {
    const database = createTestDatabase();
    const send = vi.fn(async (_message: unknown) => undefined);
    seedQueuedFeedback(database, "waiting", 1_756_000_000);

    const status = await notificationStatus(
      asD1(database),
      { ...environment(send), FEEDBACK_NOTIFICATION_FROM: "feedback@example.com" },
      new Date("2026-07-24T03:17:00Z"),
    );

    expect(status.configured).toBe(false);
    expect(status.problems).toEqual(["sender_domain_not_sendable"]);
    expect(status.from).toBe("feedback@example.com");
    expect(status.queue.pending).toBe(1);
    expect(status.queue.oldestReceivedAt)
      .toBe(new Date(1_756_000_000 * 1000).toISOString());
    expect(status.lastAttempt).toBeNull();
  });

  it("bounds an adversarial address instead of echoing it whole", async () => {
    const database = createTestDatabase();
    const send = vi.fn(async (_message: unknown) => undefined);

    const status = await notificationStatus(
      asD1(database),
      { ...environment(send), FEEDBACK_NOTIFICATION_FROM: "a".repeat(10_000) },
      new Date("2026-07-24T03:17:00Z"),
    );

    expect(status.from).toHaveLength(254);
  });

  it("does not send, claim, or reserve anything", async () => {
    const database = createTestDatabase();
    seedQueuedFeedback(database, "untouched", 1);
    const send = vi.fn(async (_message: unknown) => undefined);

    await notificationStatus(asD1(database), environment(send),
                             new Date("2026-07-24T03:17:00Z"));

    expect(send).not.toHaveBeenCalled();
    expect(queued(database)[0]?.claim_id).toBeNull();
    expect(dailyCount(database, "2026-07-24")).toBeUndefined();
    expect(attempts(database)).toHaveLength(0);
  });
});
