# Feedback email dispatch

Accepted feedback is stored in D1 immediately and queued for email separately,
so email availability never affects whether a user gets a receipt. This document
covers the half that turns that queue into mail in an inbox: how the dispatcher
decides what to send, how to activate it on a Cloudflare account, and how to
find out why it is not sending.

The client side of feedback — the wire contract, the in-app form, the endpoint
override — is in [`FEEDBACK.md`](FEEDBACK.md).

## How the dispatcher works

`src/services/feedback-intake/src/notifications.ts` holds all of it.

Each run reclaims claims older than 30 minutes, counts the unclaimed queue, and
reserves a slot for the current UTC day with a conditional upsert against
`feedback_notification_days`. The reservation is what enforces the daily
ceiling: if the day's count has already reached `FEEDBACK_MAX_DAILY_EMAILS`, the
upsert matches nothing and the run stops.

With a slot reserved, the run spreads the backlog across the day's remaining
sends — `ceil(pending / remaining slots)`, capped at 100 reports per email so a
worst-case batch stays under the provider's 5 MiB message limit — claims that
many rows, and sends one email. Claimed rows are deleted only after the send
succeeds. If it fails, both the claim and the reservation are released, so the
reports return to the queue and the day does not lose a send.

Three crons drive it, at 03:17, 11:17, and 19:17 UTC. Quarantined submissions
are never queued; they stay in the dashboard.

### What each dispatch reports

Every run returns one of five outcomes, and records it:

| Outcome | Meaning |
| --- | --- |
| `sent` | An email left the service; `messageId` identifies it to Cloudflare |
| `nothing_pending` | The dispatcher ran and the queue was empty |
| `not_configured` | Delivery configuration was rejected; `problems` says why |
| `daily_limit_reached` | The day's sends were already spent |
| `send_failed` | The provider rejected the send; `error` carries its message |

Each attempt — including the ones that send nothing — is written to
`maintenance_runs` with `operation = 'notification'` and the actor that caused
it (`scheduled`, or the administrator's Access identity). Audit retention prunes
these after `AUDIT_RETENTION_DAYS`.

## Configuration

| Setting | Where | Notes |
| --- | --- | --- |
| `FEEDBACK_EMAIL` | `send_email` binding in `wrangler.jsonc` | Exists only inside Workers |
| `FEEDBACK_NOTIFICATION_FROM` | Worker secret, from `.env` | Domain must be onboarded to Email Sending |
| `FEEDBACK_NOTIFICATION_TO` | Worker secret | Must equal the single `ADMIN_EMAILS` entry |
| `FEEDBACK_DASHBOARD_URL` | Worker secret | HTTPS, path exactly `/admin`, no port, query, or fragment |
| `ADMIN_EMAILS` | Worker secret | Exactly one address |
| `FEEDBACK_MAX_DAILY_EMAILS` | `vars` in `wrangler.jsonc`, and `.env` for the container | Defaults to 3 when unset or invalid |

`configure-cloudflare-access.sh` writes the four secrets from `.env`. The limit
is deliberately not a secret — it grants no access, so per
[`SECRETS.md`](SECRETS.md) it belongs in configuration. The deployed Worker
reads it from `vars`; the self-hosted dashboard container reads it from `.env`.

If any of the first five is wrong, the dispatcher sends nothing and says so. It
never falls back to a different sender, a different recipient, or no email at
all quietly.

## Activating it

The Cloudflare account needs a domain it can send from. This is the one step
nothing in this repository can do for you: `*.workers.dev` cannot send mail, and
Email Sending only accepts a domain on the account.

**1. Onboard a sending domain.**

```sh
cd src/services/feedback-intake
npx wrangler email sending enable example-domain-you-own.com
npx wrangler email sending dns get example-domain-you-own.com
```

Onboarding adds the SPF and DKIM records automatically if the domain's DNS is on
Cloudflare. Confirm they resolve before continuing — usually 5–15 minutes.

**2. Prove the provider before involving the service.** If this fails, nothing
downstream can work, and it fails in one place instead of five.

```sh
npx wrangler email sending send \
  --from feedback@example-domain-you-own.com \
  --to you@your-inbox.example \
  --subject "Practice Takes dispatcher check" \
  --text "Provider reachable."
```

**3. Apply the migrations to the remote database.**

```sh
npm run db:migrate:remote
```

**4. Set the real sender and push the secrets.** Edit
`src/services/feedback-intake/.env` so `FEEDBACK_NOTIFICATION_FROM` is the
address you just proved, then:

```sh
python3 tools/scripts/secrets/secrets_manager.py encrypt
tools/scripts/feedback/configure-cloudflare-access.sh
```

The script refuses a sender on `example.com`, a `.invalid`/`.test`/`.local`
domain, or a `workers.dev` host, because none of them can ever deliver.

**5. Deploy.**

```sh
npm run deploy --workspace @practice-takes/feedback-intake
```

**6. Confirm the service agrees.** Both routes are behind the same Cloudflare
Access application as the dashboard, so use a browser session or an Access
service token.

```sh
curl https://<host>/v1/admin/notifications
```

`configured` must be `true` and `problems` empty. If not, the problem codes
below say what to fix.

**7. Send for real.**

```sh
curl -X POST https://<host>/v1/admin/notifications/dispatch
```

A `sent` outcome with a `messageId`, an email in the inbox, and a drained queue
means the dispatcher is live. If the queue is empty, submit one report through
the application first — that exercises the real path end to end, which a
synthetic test message would not.

## The administrative API

Both routes require an authorized Access identity and return JSON.

### `GET /v1/admin/notifications`

Reads only. It never sends, claims, or reserves a slot.

```json
{
  "notifications": {
    "configured": true,
    "problems": [],
    "from": "feedback@example-domain-you-own.com",
    "to": "you@your-inbox.example",
    "dashboardUrl": "https://host/admin",
    "queue": { "pending": 2, "claimed": 0, "oldestReceivedAt": "2026-08-09T18:00:00.000Z" },
    "daily": { "day": "2026-08-09", "sent": 1, "limit": 3, "remaining": 2 },
    "lastAttempt": {
      "actor": "scheduled",
      "outcome": "sent",
      "completedAt": "2026-08-09T03:17:02.000Z",
      "sent": 4,
      "problems": [],
      "error": null
    }
  }
}
```

`claimed` above zero with an old `oldestReceivedAt` means a run is in flight or
died mid-batch; claims older than 30 minutes are reclaimed by the next run.

### `POST /v1/admin/notifications/dispatch`

Runs the same code the cron runs, with your Access identity as the actor. It
sends only genuinely queued feedback and it **consumes one of the day's sends** —
there is no override, because a ceiling any caller can waive is not a ceiling.
If you need more sends today, raise `FEEDBACK_MAX_DAILY_EMAILS` and deploy.

`200` for `sent`, `nothing_pending`, and `daily_limit_reached`. `503` for
`not_configured` and `send_failed`, with the same `dispatch` body plus an
`error` object, so a script can branch on the status code.

```json
{
  "dispatch": {
    "outcome": "sent",
    "sent": 2,
    "pending": 0,
    "dailyLimit": 3,
    "dailyEmailNumber": 1,
    "remainingDailyEmails": 2,
    "messageId": "…",
    "problems": [],
    "error": null
  }
}
```

## Problem codes

| Code | What to do |
| --- | --- |
| `missing_email_binding` | No `send_email` binding. Expected outside Workers; in the Worker, check `wrangler.jsonc` and redeploy |
| `missing_from_address` | `FEEDBACK_NOTIFICATION_FROM` is unset — rerun `configure-cloudflare-access.sh` |
| `invalid_from_address` | Not a single well-formed address under 254 characters |
| `sender_domain_not_sendable` | The sender is on a domain that can never send: `example.*`, `.invalid`, `.test`, `.local`, `localhost`, or `workers.dev`. This is the state a fresh `.env` starts in |
| `missing_to_address` / `invalid_to_address` | Same, for `FEEDBACK_NOTIFICATION_TO` |
| `administrator_not_configured` | `ADMIN_EMAILS` is empty; the dashboard is also unusable in this state |
| `multiple_administrators` | `ADMIN_EMAILS` holds more than one address. Exactly one is allowed |
| `recipient_not_administrator` | The recipient is not the administrator. Refused so a configuration change cannot redirect private feedback |
| `missing_dashboard_url` / `invalid_dashboard_url` | Must be HTTPS with the path exactly `/admin`, no credentials, port, query, or fragment |

A domain that is real but simply not onboarded yet passes validation and fails
at send time instead, as `send_failed` with the provider's message. Validation
rejects what can never work; the provider rejects what does not work yet.

## When mail stops arriving

1. `GET /v1/admin/notifications`. `configured: false` names the cause directly.
2. Check `lastAttempt`. A stale `completedAt` means the crons are not running,
   not that delivery is broken — check the Worker's deployment and triggers.
3. Repeated `send_failed` with a growing `queue.pending`: the configuration is
   accepted but the provider is refusing. Re-run step 2 of activation to test
   the provider without the service in the way, and check SPF and DKIM still
   resolve.
4. `daily_limit_reached` with a large backlog is working as designed — the queue
   drains across the following days' sends. Raise the limit if that is too slow.
