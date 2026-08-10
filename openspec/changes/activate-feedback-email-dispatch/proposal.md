## Why

The feedback email dispatcher is fully written and completely inert, and
nothing in the system says so.

`sendPendingFeedbackBatch` runs on three daily crons, drains
`feedback_email_queue`, and sends a batch through the `FEEDBACK_EMAIL` binding.
Before any of that, `notificationConfiguration` checks the sender address, the
recipient address, the administrator allow-list, and the dashboard URL, and
returns `null` if any one of them is wrong. The dispatcher then returns `0`.
Zero is also what it returns when the queue is empty, when the daily limit is
reached, and when the send throws. All four are silent, indistinguishable, and
unobservable — the only trace is a `console.error` that nobody is watching.

The production configuration is in exactly that state. The deployed
`FEEDBACK_NOTIFICATION_FROM` comes from `.env`, which still holds the example
value `…@example.com`. Feedback has been accumulating in D1 and queueing for
email that was never going to arrive. There is no endpoint, no dashboard field,
and no log a human reads that would have revealed this at any point.

Activation therefore needs two things that do not exist: a way to ask the
service what it thinks its email configuration is, and a way to make it try
right now instead of waiting up to eight hours for a cron that will fail
silently again.

## What Changes

- **The dispatcher reports why it did nothing.** `sendPendingFeedbackBatch`
  returns a result object — `outcome`, `sent`, `pending`, the daily counters,
  the provider message ID, and an error string — instead of a bare count. The
  five distinct outcomes (`sent`, `nothing_pending`, `not_configured`,
  `daily_limit_reached`, `send_failed`) stop collapsing into `0`.
- **Configuration failures are itemised.** `notificationConfiguration` returns
  the list of machine-readable problems it found (`missing_email_binding`,
  `invalid_from_address`, `recipient_not_administrator`, …) rather than `null`.
  The exact placeholder-sender case that is live in production today is named
  by `sender_domain_not_sendable`.
- **New administrative API.** `GET /v1/admin/notifications` reports the
  configuration verdict, the queue depth and oldest queued report, today's send
  count against the limit, and the last dispatch attempt with its outcome.
  `POST /v1/admin/notifications/dispatch` runs a real batch immediately under
  the caller's identity. Both sit behind the existing Cloudflare Access
  application, like every other `/v1/admin/` route.
- **Every dispatch attempt is recorded.** Each run — scheduled or manual, and
  including the ones that send nothing — writes a `maintenance_runs` row with
  its actor and outcome, so "when did this last work" has an answer that
  survives the request. Existing audit retention prunes it.
- **The daily email limit becomes configuration.** `FEEDBACK_MAX_DAILY_EMAILS`
  replaces the hardcoded `3`, defaulting to `3` when unset or invalid.
- **BREAKING (schema) — the daily-count table loses its hardcoded ceiling.**
  `feedback_notification_days.sent_count` carries
  `CHECK (sent_count BETWEEN 0 AND 3)`. Any configured limit above three would
  fail that constraint at the exact moment the fourth email of a busy day
  mattered. Migration `0008` rebuilds the table with `CHECK (sent_count >= 0)`
  and widens `maintenance_runs.operation` to admit `'notification'`. SQLite
  cannot alter a `CHECK`, so both are table rebuilds.
- **An activation runbook.** `docs/development/operations/EMAIL_DISPATCH.md`
  covers onboarding a sending domain, the DNS records, running the existing
  `configure-cloudflare-access.sh`, and verifying through the new API. The
  setup script gains a guard that refuses a sender on a reserved example
  domain or a `workers.dev` host, so the failure that is live today cannot be
  re-introduced silently.

## Non-goals

- **No dashboard UI.** The two routes are the deliverable; rendering them in
  `admin.html` is a separate change.
- **No retry or alerting policy.** A failed send still returns its report to
  the queue for the next cron. Whether a repeatedly failing dispatcher should
  escalate is a real question and not this one.
- **No change to what the email contains,** to the batching arithmetic, or to
  the 100-reports-per-email bound that keeps a batch under the provider's
  5 MiB message limit.

## Capabilities

### New Capabilities

- `feedback-email-dispatch`: what the dispatcher must be able to tell an
  operator about itself, what a manual dispatch may and may not do, and the
  rule that a configured delivery path is never silently substituted for a
  broken one.

### Modified Capabilities

<!-- None. `worker-database-fidelity` requires that worker tests run against a
     schema built from the migration files; migration 0008 is picked up by that
     mechanism with no test-side schema change. -->

## Impact

- `src/services/feedback-intake/src/notifications.ts` — result object,
  itemised configuration diagnostics, configurable limit, dispatch recording,
  and the status query.
- `src/services/feedback-intake/src/admin.ts` — two routes; `AdminEnv` extends
  `NotificationEnv`.
- `src/services/feedback-intake/src/index.ts` — the scheduled handler names
  itself as the actor.
- `src/services/feedback-intake/src/docker-server.ts` — forwards the
  notification variables so the self-hosted dashboard reports the same status,
  minus the binding it cannot have.
- `src/services/feedback-intake/migrations/0008_notification_dispatch.sql` —
  new.
- `src/services/feedback-intake/wrangler.jsonc`, `.env.example` —
  `FEEDBACK_MAX_DAILY_EMAILS`.
- `src/services/feedback-intake/test/{notifications,admin,migrations}.test.ts` —
  the existing notification tests assert against the returned count and move to
  `result.sent`.
- `tools/scripts/feedback/configure-cloudflare-access.sh` — sender-domain guard.
- `docs/development/operations/EMAIL_DISPATCH.md` (new),
  `docs/development/operations/FEEDBACK.md`,
  `docs/development/README.md`, `src/services/feedback-intake/README.md`.
- **Cloudflare — the part this repository cannot do.** The account currently
  has no zone, so there is no domain to send from; `*.workers.dev` cannot be a
  sender. A domain must be added to the account and onboarded to Email
  Sending before any of the above delivers mail. The runbook covers it and the
  status endpoint confirms it, but the change lands unactivated until that is
  done.
- **Not affected:** the C++ application, the public intake routes, the feedback
  wire contract, and the content of the email itself.
