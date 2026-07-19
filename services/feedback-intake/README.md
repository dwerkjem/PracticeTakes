# Feedback intake service

This Cloudflare Worker is the application-facing service for issue #63. It exposes only two
write-oriented routes:

- `POST /v1/authorizations` issues a five-minute, installation- and client-bound authorization.
- `POST /v1/submissions` consumes that authorization once and returns a receipt identifier.

There are deliberately no listing, search, export, update, or deletion routes. Administrative
access uses Cloudflare's authenticated D1 tooling and must be granted separately from the
Worker's runtime binding.

## Configuration and deployment

1. Install Node.js and npm, then run `npm install` in this directory.
2. Create the database with `npx wrangler d1 create practice-takes-feedback`.
3. Copy `wrangler.example.jsonc` to the ignored local file `wrangler.jsonc`, then copy the
   database ID into it. Do not commit account IDs, tokens, or secret values.
4. Apply migrations with `npm run db:migrate:remote`.
5. Run `npm run deploy` once to create the Worker. With `workers_dev` disabled and no custom
   route configured yet, this initial deployment is not publicly reachable.
6. Generate and configure the production signing key:
   `openssl rand -hex 32 | npx wrangler secret put SUBMISSION_SIGNING_KEY`.
7. Confirm it with `npx wrangler secret list`, then configure a custom HTTPS route in the
   Cloudflare dashboard.

`workers_dev` is disabled so deployment requires an intentional route. Cloudflare API credentials
used for deployment should be scoped to this Worker and D1 database. The runtime receives only the
`FEEDBACK_DB` binding and signing secret; it does not receive account-level administration tokens.

## Versioned request format

The canonical application/service contract is
[`contracts/feedback/v1.schema.json`](../../contracts/feedback/v1.schema.json). Both sides should
change the schema version when making a breaking protocol change.

Request an authorization:

```json
{
  "schemaVersion": 1,
  "appVersion": "0.2.6",
  "installationId": "a-random-installation-identifier"
}
```

Then submit feedback using the returned authorization:

```json
{
  "schemaVersion": 1,
  "authorization": "payload.signature",
  "submittedAt": "2026-07-18T20:00:00.000Z",
  "appVersion": "0.2.6",
  "installationId": "a-random-installation-identifier",
  "category": "bug",
  "message": "The tuner briefly displayed the wrong octave.",
  "contactEmail": "optional@example.com"
}
```

The service stores hashes of the installation identifier and client address, not their raw values.
Authorization is bound to both hashes and cannot be replayed after successful consumption.

## Local smoke test

From the `services` directory, install the pinned toolchain and initialize local D1 state:

```bash
npm install
printf 'SUBMISSION_SIGNING_KEY="%s"\n' "$(openssl rand -hex 32)" > feedback-intake/.dev.vars
npm run db:migrate:local --workspace @practice-takes/feedback-intake
npm run dev --workspace @practice-takes/feedback-intake -- --local-protocol https
```

Wrangler uses a self-signed local certificate, so the following smoke-test requests use `curl -k`.
In another terminal, request a short-lived authorization:

```bash
AUTH_RESPONSE=$(curl -sk https://localhost:8787/v1/authorizations \
  -H 'Content-Type: application/json' \
  --data '{"schemaVersion":1,"appVersion":"0.2.6","installationId":"local-test-installation-0001"}')
printf '%s\n' "$AUTH_RESPONSE"
AUTHORIZATION=$(printf '%s' "$AUTH_RESPONSE" | jq -r '.authorization')
```

Use it to submit feedback:

```bash
SUBMITTED_AT=$(date -u +%Y-%m-%dT%H:%M:%S.000Z)
curl -sk https://localhost:8787/v1/submissions \
  -H 'Content-Type: application/json' \
  --data "{\"schemaVersion\":1,\"authorization\":\"$AUTHORIZATION\",\"submittedAt\":\"$SUBMITTED_AT\",\"appVersion\":\"0.2.6\",\"installationId\":\"local-test-installation-0001\",\"category\":\"bug\",\"message\":\"Local feedback smoke test.\"}"
```

A successful response has status `201` and contains a `receiptId`. Repeating the identical
submission must return `409 duplicate_submission`. Stop Wrangler with Ctrl-C. The `.dev.vars`
secret and `.wrangler` local database state are ignored by Git.

## Current abuse controls

- 16 KiB request limit and field-specific size checks
- configurable minimum supported semantic application version
- timestamps constrained to a ten-minute window
- five-minute HMAC authorizations with unique token IDs
- one-use token storage enforced by a database uniqueness constraint
- per-client authorization limits and per-client/per-installation submission limits

Rate-limit defaults live in `wrangler.jsonc`. Cloudflare WAF rules may add an outer IP limit without
changing the application protocol.
