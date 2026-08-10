## 1. Schema

- [x] 1.1 Add `migrations/0008_notification_dispatch.sql`: rebuild `feedback_notification_days` with `CHECK (sent_count >= 0)` in place of `BETWEEN 0 AND 3`, copying existing rows; rebuild `maintenance_runs` with `CHECK (operation IN ('retention', 'notification'))`, preserving `AUTOINCREMENT` and both indexes
- [x] 1.2 Extend `migrations.test.ts`: the table set is unchanged, a `sent_count` above three is accepted, and a `maintenance_runs` row with `operation = 'notification'` inserts while an unknown operation still fails

## 2. The dispatcher

- [x] 2.1 Replace the `number` return of `sendPendingFeedbackBatch` with `DispatchResult`, and take the actor as a parameter
- [x] 2.2 Turn `notificationConfiguration` into a union returning either the configuration or the full `problems` list, collecting all problems rather than short-circuiting
- [x] 2.3 Add `sender_domain_not_sendable` validation for the reserved example domains, `.invalid`/`.test`/`.local`, `localhost`, and `workers.dev` hosts
- [x] 2.4 Read the daily limit from `FEEDBACK_MAX_DAILY_EMAILS` via `configuredPositiveInteger`, defaulting to three
- [x] 2.5 Capture `messageId` from the send result instead of discarding it
- [x] 2.6 Record every attempt as a `maintenance_runs` row with `operation = 'notification'`, the actor, and the result as `details_json`; a recording failure must not change the dispatch outcome
- [x] 2.7 Add `notificationStatus(db, env, now)` returning the configuration verdict, queue depth, oldest queued timestamp, today's counters, and the last recorded attempt

## 3. The administrative API

- [x] 3.1 `AdminEnv` extends `NotificationEnv`; `admin.ts` imports the dispatcher
- [x] 3.2 `GET /v1/admin/notifications` returns the status report; it sends nothing and writes nothing
- [x] 3.3 `POST /v1/admin/notifications/dispatch` runs a dispatch with the Access identity as the actor; 200 for `sent`, `nothing_pending`, and `daily_limit_reached`, 503 for `not_configured` and `send_failed`, with the same body shape either way
- [x] 3.4 The scheduled handler in `index.ts` passes `"scheduled"` as the actor
- [x] 3.5 `docker-server.ts` forwards the notification variables so the self-hosted dashboard reports the same status without a binding it cannot have

## 4. Configuration

- [x] 4.1 Add `FEEDBACK_MAX_DAILY_EMAILS` to `wrangler.jsonc` `vars` and to `.env.example`, both at `3`
- [x] 4.2 Guard `configure-cloudflare-access.sh` against a sender on a reserved example domain or a `workers.dev` host, refusing with the reason

## 5. Tests

- [x] 5.1 Move the fifteen count assertions in `notifications.test.ts` to `result.sent`
- [x] 5.2 Cover each of the five outcomes, including that `not_configured` leaves the queue unclaimed and that `send_failed` releases both the claim and the reservation
- [x] 5.3 Cover the problem list: several problems reported at once, the recipient/administrator mismatch, and the unsendable-sender cases
- [x] 5.4 Cover a configured limit above three sending a fourth batch in one UTC day, and the fallback to three for absent, non-numeric, zero, and negative values
- [x] 5.5 Cover attempt recording: scheduled and manual actors, an attempt recorded when nothing was pending, and a recording failure leaving the outcome intact
- [x] 5.6 Cover both routes in `admin.test.ts`, including the unauthenticated refusal, the status route sending nothing, and the 503 outcomes
- [x] 5.7 `npm run check && npm run test` from `src/services` passes

## 6. Documentation

- [x] 6.1 Write `docs/development/operations/EMAIL_DISPATCH.md`: how the dispatcher works, what has to exist on the Cloudflare account, the activation order, how to verify each step, and how to read every problem code
- [x] 6.2 Correct the "three scheduled runs … at most three emails" claim in `docs/development/operations/FEEDBACK.md` now that the limit is configurable
- [x] 6.3 Index the runbook in `docs/development/README.md`
- [x] 6.4 Document the two routes and `FEEDBACK_MAX_DAILY_EMAILS` in `src/services/feedback-intake/README.md`

## 7. Activation — needs the Cloudflare account

- [ ] 7.1 Add a domain to the account and onboard it to Email Sending: `npx wrangler email sending enable <domain>`, then `npx wrangler email sending dns get <domain>` and confirm SPF and DKIM resolve
- [ ] 7.2 Prove the provider independently of the service: `npx wrangler email sending send --from <sender> --to <administrator> --subject 'Practice Takes dispatcher check' --text …`
- [ ] 7.3 Apply migration 0008 remotely: `npm run db:migrate:remote`
- [ ] 7.4 Set the real `FEEDBACK_NOTIFICATION_FROM` in `.env`, re-encrypt, and re-run `tools/scripts/feedback/configure-cloudflare-access.sh`
- [ ] 7.5 Deploy the Worker
- [ ] 7.6 `GET /v1/admin/notifications` and confirm it reports delivery as configured with no problems
- [ ] 7.7 `POST /v1/admin/notifications/dispatch` and confirm the outcome, the arriving email, and that the queue drained
