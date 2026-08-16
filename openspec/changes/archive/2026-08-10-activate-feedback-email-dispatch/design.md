## Context

`sendPendingFeedbackBatch` (`src/services/feedback-intake/src/notifications.ts`)
is the whole dispatcher. It reclaims stale claims, counts the pending queue,
reserves a slot in `feedback_notification_days` with a conditional upsert,
claims a batch with `UPDATE … RETURNING`, sends one email, and deletes the
claimed rows. On a send failure it releases both the claim and the reservation,
so nothing is lost and the next cron retries. That core is sound and this change
does not touch it.

What it lacks is a voice. Every one of the following returns `0`:

| Situation | Line |
| --- | --- |
| Configuration invalid or binding absent | `if (!configuration) return 0` |
| Nothing queued | `if (pendingCount < 1) return 0` |
| Daily limit already spent | `if (!reservation) return 0` |
| The claim raced and came back empty | `if (reports.length === 0) return 0` |
| The provider rejected the send | the `catch`, after `console.error` |

The caller is `context.waitUntil(...)` in the scheduled handler, which discards
the value. So the return type has, until now, been decorative.

The production instance is in the first row of that table. `.env` still carries
`FEEDBACK_NOTIFICATION_FROM=…@example.com` from the template, and
`configure-cloudflare-access.sh` pushed that value to the Worker verbatim. The
account has no zone at all, so no sender address could have worked.

Two constraints shape everything below. The `send_email` binding requires the
sender's domain to be onboarded to Cloudflare Email Sending — a
`*.workers.dev` host cannot send, and nothing in the repository can onboard a
domain the account does not own. And `feedback_notification_days.sent_count`
carries `CHECK (sent_count BETWEEN 0 AND 3)`, written when three was a constant
rather than a setting.

## Goals / Non-Goals

**Goals**

- An operator can determine, in one authenticated GET, whether email delivery
  is configured and what specifically is wrong if it is not.
- An operator can force a real dispatch and see its outcome in the response.
- The distinction between "nothing to send" and "could not send" survives the
  request, so the next question — "has this ever worked?" — is answerable.
- The daily ceiling is configuration, and the schema does not contradict it.

**Non-Goals**

- A dashboard rendering of any of this.
- Retry policy, escalation, or alerting beyond the existing next-cron retry.
- Changing the email body, subject, or batching arithmetic.
- Sending anything that is not real queued feedback (see Decision 4).

## Decisions

### 1. A result object, not a count

`sendPendingFeedbackBatch` returns:

```ts
interface DispatchResult {
  outcome: "sent" | "nothing_pending" | "not_configured"
         | "daily_limit_reached" | "send_failed";
  sent: number;
  pending: number;
  dailyEmailNumber: number | null;
  dailyLimit: number;
  remainingDailyEmails: number;
  messageId: string | null;
  problems: string[];
  error: string | null;
}
```

`problems` is populated only for `not_configured`. `messageId` comes from
`EmailSendResult`, which the current code discards — it is the only handle a
human has for asking Cloudflare what happened to a specific message.

*Alternative rejected:* keep the count and add a separate `notificationStatus()`
that re-derives the reason. It would have to re-run the same validation to
answer "why zero", and the two could disagree — precisely the class of bug this
change exists to remove.

The existing `notifications.test.ts` asserts on the count at fifteen call sites.
Those become `result.sent`. This is a mechanical change to an internal
interface with one production caller.

### 2. Itemised problems rather than a null

`notificationConfiguration` becomes a discriminated union: either the resolved
configuration, or `{ problems: string[] }`. Problem codes are stable and
machine-readable:

`missing_email_binding`, `missing_from_address`, `invalid_from_address`,
`sender_domain_not_sendable`, `missing_to_address`, `invalid_to_address`,
`administrator_not_configured`, `multiple_administrators`,
`recipient_not_administrator`, `missing_dashboard_url`,
`invalid_dashboard_url`.

All problems are collected, not short-circuited at the first — an operator
fixing configuration one round-trip at a time is the exact scenario this
endpoint serves.

`sender_domain_not_sendable` is new validation, not just new reporting: a
sender at `example.com`, `example.org`, `example.net`, `.invalid`, `.test`,
`.local`, `localhost`, or any `*.workers.dev` host can never be onboarded to
Email Sending, so accepting it means queueing mail that provably cannot leave.
This is the check that would have caught the live misconfiguration on day one.

*Note on the validated set:* it rejects what is definitionally unsendable, not
everything unsendable. A real domain that simply has not been onboarded still
passes validation and fails at send time — as `send_failed`, with the
provider's message, which is the honest answer.

### 3. Manual dispatch is the same code path, differing only in actor

`POST /v1/admin/notifications/dispatch` calls `sendPendingFeedbackBatch` with
the caller's Access identity as the actor. It reserves a daily slot, honours
the configured limit, sends only real queued reports, and is subject to every
rule the cron is. A manual trigger that took a different path would be testing
something other than the thing that runs at 03:17.

Consequently a manual dispatch **consumes** one of the day's sends. The limit
is a promise about the administrator's inbox, and a promise that any
authenticated caller may break is not one. The limit being configurable is what
makes this comfortable: an operator who needs more sends today raises
`FEEDBACK_MAX_DAILY_EMAILS`, which is visible, deployed, and auditable, rather
than reaching for a bypass flag that is none of those.

*Alternative rejected:* an `?ignoreLimit=true` parameter. It makes the bound
advisory, and every future caller has to be trusted to not pass it.

### 4. No synthetic test email

Considered and dropped. A `POST …/test` that sends fabricated content proves
the binding and the DNS work, and proves nothing about the queue, the claim
protocol, the daily reservation, or the batch formatting — the parts with
actual logic. Meanwhile it needs its own quota, its own template, and its own
tests, and it introduces a second way to send mail that must be kept honest
against the first.

The equivalent capability already exists outside the Worker and is better for
the job, because it isolates the provider from the application entirely:

```sh
npx wrangler email sending send --from … --to … --subject … --text …
```

The runbook uses that to prove the domain, then a real dispatch to prove the
service. When the queue is genuinely empty, submitting one feedback report
through the app is a more faithful test than any synthetic message.

### 5. `maintenance_runs` records dispatches; a new table does not

The table already exists for exactly this — `operation`, `actor`,
`started_at`, `completed_at`, `details_json` — is already indexed by
`(operation, completed_at DESC)`, already pruned by audit retention, and
`operationalReport` already reads its retention rows the same way the status
endpoint will read notification rows. Its `CHECK (operation IN ('retention'))`
widens to admit `'notification'`.

Attempts that send nothing are recorded too. "The dispatcher ran at 03:17 and
found nothing pending" and "the dispatcher has not run since Tuesday" are
different facts, and only recording sends conflates them. At three crons a day
plus occasional manual runs, this is roughly a thousand rows a year against a
730-day retention.

*Alternative rejected:* `admin_action_receipts`. Its `CHECK` admits only
`create`/`update`/`delete` and it requires a `receipt_id`; a dispatch has
neither. Recording the actor in `maintenance_runs` satisfies the audit need.

### 6. Migration 0008 rebuilds two tables

SQLite cannot alter a `CHECK` constraint, so both changes are
create-copy-drop-rename. `feedback_notification_days` gains
`CHECK (sent_count >= 0)`; `maintenance_runs` gains `'notification'` and keeps
its `AUTOINCREMENT` primary key and both indexes.

The rebuild is safe on live data: `feedback_notification_days` holds at most a
handful of rows and is rebuilt from itself, and no foreign key references
either table. D1 applies each migration file as a unit.

### 7. The limit is read from the environment, which means two files

`FEEDBACK_MAX_DAILY_EMAILS` is read with the existing
`configuredPositiveInteger` helper, so it is a plain environment variable and
every deployment path already carries it: `vars` in `wrangler.jsonc` for the
deployed Worker, `.dev.vars` for `wrangler dev`, and `.env` for the self-hosted
dashboard container.

It is deliberately **not** a Worker secret. `SECRETS.md` is explicit that values
granting no access do not belong in the secret set, and a send-count ceiling
grants nothing. Both `wrangler.jsonc` and `.env.example` get the value so that
neither deployment path is the one that silently keeps the old default.

## Risks / Trade-offs

- **The change cannot be verified end-to-end in this repository.** Everything
  here is testable against the SQLite-backed harness and a stubbed binding, and
  none of it proves an email arrives. Proof requires a domain on the account.
  Mitigation: the runbook orders the external steps so the first failure is the
  cheap one (`wrangler email sending send`, no application involved), and the
  status endpoint states the verdict rather than implying it.
- **Manual dispatch spends a daily send.** An operator testing repeatedly can
  exhaust the day's quota and then see `daily_limit_reached` instead of
  delivery. The outcome names itself and reports `remainingDailyEmails`, and
  the limit is now a variable. Accepted, per Decision 3.
- **`sender_domain_not_sendable` could refuse something legitimate.** Someone
  running a private root CA and an internal `.local` mail domain would be
  blocked. Cloudflare Email Sending cannot serve that case anyway, so the check
  refuses only what the provider would refuse later and less clearly.
- **Two homes for the limit can drift.** `wrangler.jsonc` and `.env` could
  disagree. They configure different deployments, so this is per-deployment
  configuration rather than duplication — and the status endpoint reports the
  effective value each one is actually using.
- **Recording every attempt writes on a path that currently does not.** One
  extra insert per cron run, off the request path entirely, inside
  `waitUntil`.

## Migration Plan

1. Migration `0008` applies locally and to the remote D1 before the Worker is
   deployed. It is backwards compatible: the current code neither reads the
   widened `CHECK` nor writes `'notification'` rows.
2. Deploy the Worker. With configuration still invalid, behaviour is unchanged
   except that `GET /v1/admin/notifications` now says why.
3. Onboard a sending domain, then re-run `configure-cloudflare-access.sh` with
   a real `FEEDBACK_NOTIFICATION_FROM`. The script now refuses the placeholder.
4. `POST /v1/admin/notifications/dispatch` and read the outcome.

Rollback is a redeploy of the previous Worker; the migration can stay, since it
only relaxes constraints the old code satisfies.

## Open Questions

- **Which domain sends the mail?** The account has no zone. The tests use
  `feedback@practicetakes.app`, which suggests the intent, but the domain is not
  on the account. Deferred to the operator — the runbook takes the domain as
  input rather than assuming one.
- **Should a run of consecutive `send_failed` outcomes surface anywhere other
  than the status endpoint?** Once dispatch history exists, that becomes
  answerable; deliberately out of scope here.
