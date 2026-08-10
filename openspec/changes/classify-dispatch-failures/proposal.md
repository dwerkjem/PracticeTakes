## Why

CodeQL alert 8 (`js/stack-trace-exposure`, medium) traces a caught exception's
`message` from `notifications.ts` into the JSON body built at `admin.ts:533`.

The real exposure is small — the route is admin-only behind Cloudflare Access,
and anyone who reaches it can already read every submission — but the shape is
the flagged one, and a regex over the message does not change that. Tainted data
still reaches a response, so the alert would return on every scan and have to be
dismissed again each time.

There is also a design argument independent of the alert. A free-text provider
string is not something a caller can branch on. Every consumer that wants to
distinguish "the domain is not verified" from "we were rate limited" has to
pattern-match English, which changes without warning when the provider rewrites
its errors.

## What Changes

- **BREAKING (API) — `DispatchResult.error` becomes `failureReason`,** an
  enumerated code rather than the provider's text: `provider_rejected`,
  `provider_unavailable`, `provider_rate_limited`, or `unknown`. Classification
  happens inside the dispatcher, against the raw message, which never leaves it.
- **The raw provider message goes to `console.error` only.** It is already
  logged there in full; this change stops it also being returned and stops it
  being written into the recorded attempt, so it cannot resurface through
  `lastAttempt` on the status route.
- **`DispatchAttempt` and the status payload follow,** carrying `failureReason`
  in place of `error`.
- **The administrative 503 body carries a fixed sentence per reason** rather
  than the provider's words.

## Non-goals

- **No change to the `not_configured` problem list.** Those codes are already
  enumerated and chosen by our own code; nothing there is derived from an
  exception.
- **No change to which outcomes exist,** to the queue protocol, or to the daily
  limit.
- **No retry policy keyed off the new reasons.** `provider_unavailable` is
  plainly the retryable one, but acting on that is a separate change.

## Capabilities

### Modified Capabilities

- `feedback-email-dispatch`: what a failed dispatch reports, and where the
  provider's own words are allowed to go.

## Impact

- `src/services/feedback-intake/src/notifications.ts` — `failureReason`,
  the classifier, `reportableError` removed.
- `src/services/feedback-intake/src/admin.ts` — fixed message per reason.
- `src/services/feedback-intake/test/{notifications,admin}.test.ts`.
- `docs/development/operations/EMAIL_DISPATCH.md`,
  `src/services/feedback-intake/README.md`.
- **Diagnosability.** An operator reading a 503 learns the class of failure, not
  the provider's sentence. Recovering the sentence means `wrangler tail` or the
  Worker's logs. This is the accepted cost; the runbook says so plainly.
- **Not affected:** the C++ application, the public intake routes, the wire
  contract, the schema. No migration.
