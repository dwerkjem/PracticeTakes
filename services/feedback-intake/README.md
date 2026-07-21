# Practice Takes feedback service

This Cloudflare Worker accepts feedback from the Practice Takes desktop application and stores it
in Cloudflare D1. It also serves a private inbox where authorized developers can search, organize,
and export submissions.

The public API is write-only:

- `POST /v1/authorizations` issues a five-minute, installation- and client-bound authorization.
- `POST /v1/submissions` consumes that authorization once and returns a receipt identifier.

The administrative interface is protected by Cloudflare Access and an application-level email
allowlist. Administrative responses always include `Cache-Control: no-store`.

- `GET /admin` serves the feedback inbox.
- `GET /admin/audit` serves the administrator action receipt history.
- `POST /v1/admin/submissions` creates an administrative feedback record.
- `GET /v1/admin/submissions` searches and filters submissions.
- `GET /v1/admin/submissions/:receiptId` returns one submission.
- `PATCH /v1/admin/submissions/:receiptId` updates feedback and triage metadata.
- `DELETE /v1/admin/submissions/:receiptId` deletes a submission after confirmation.
- `POST /v1/admin/submissions/batch-delete` deletes selected submissions in one request.
- `GET /v1/admin/audit` lists administrator action receipts.
- `GET /v1/admin/export?id=...` exports selected records as CSV.

## Feedback triage

The inbox can search user text, notes, tags, and contact email, and filter by category, application
version, platform text, received date, workflow status, and priority. Each submission supports the
statuses New, Needs review, Planned, Duplicate, Resolved, and Declined, plus developer notes,
priority, tags, a resulting GitHub issue URL, and a duplicate target receipt. Associating a
duplicate preserves both original records and marks the referring report Duplicate.

Every administrative create, update, and delete writes an append-only action receipt containing
the administrator email, action, affected receipt ID, changed fields, and timestamp. These receipts
are available on the separate **Admin receipts** page. Delete receipts also preserve the feedback
title so the removed record remains identifiable after its content is gone.

The selection checkboxes support exporting or deleting multiple records. **Save all** persists
every feedback card currently visible in the inbox, while each card retains its individual save
button for focused edits.

The client includes its environment at the end of each message. The administrative API presents
that value separately as `diagnosticContext` and leaves the user-authored content in
`userFeedback`. Selected inbox records can be downloaded as CSV for backup or offline analysis.

## Project layout

- `src/index.ts` handles authorization and feedback submission.
- `src/admin.ts` provides the private inbox and administrative API.
- `src/admin.html`, `src/admin.css`, and `src/dashboard.js` contain the dashboard structure,
  styling, and browser-side behavior.
- `migrations/` contains the D1 schema migrations.
- `test/` contains the intake and administration test suites.
- [`contracts/feedback/v1.schema.json`](../../contracts/feedback/v1.schema.json) defines the wire
  format shared with the desktop application.

## Local development

From the `services` directory, install dependencies and initialize the local D1 database:

```bash
npm install
printf 'SUBMISSION_SIGNING_KEY="%s"\n' "$(openssl rand -hex 32)" > feedback-intake/.dev.vars
npm run db:migrate:local --workspace @practice-takes/feedback-intake
npm run dev --workspace @practice-takes/feedback-intake -- --local-protocol https
```

Wrangler uses a self-signed local certificate, so the smoke-test requests below use `curl -k`.
Request a short-lived submission authorization:

```bash
AUTH_RESPONSE=$(curl -sk https://localhost:8787/v1/authorizations \
  -H 'Content-Type: application/json' \
  --data '{"schemaVersion":1,"appVersion":"0.2.6","installationId":"local-test-installation-0001"}')
printf '%s\n' "$AUTH_RESPONSE"
AUTHORIZATION=$(printf '%s' "$AUTH_RESPONSE" | jq -r '.authorization')
```

Submit feedback with the returned authorization:

```bash
SUBMITTED_AT=$(date -u +%Y-%m-%dT%H:%M:%S.000Z)
curl -sk https://localhost:8787/v1/submissions \
  -H 'Content-Type: application/json' \
  --data "{\"schemaVersion\":1,\"authorization\":\"$AUTHORIZATION\",\"submittedAt\":\"$SUBMITTED_AT\",\"appVersion\":\"0.2.6\",\"installationId\":\"local-test-installation-0001\",\"category\":\"bug\",\"message\":\"Local feedback smoke test.\"}"
```

A successful submission returns `201` with a `receiptId`. Reusing the same authorization returns
`409 duplicate_submission`. The `.dev.vars` secret and `.wrangler` database state are ignored by
Git.

## Self-hosted dashboard container

The Docker image runs only the administrative dashboard. Feedback remains in the existing
cloud-hosted D1 database; the container queries it through Cloudflare's D1 API. Its API token should
be scoped to D1 read and write access for only the required account and rotated independently from
the submission signing key.

From `services/feedback-intake`, copy `.env.example` to `.env` and provide the Cloudflare account
ID, D1 database ID, scoped API token, administrator email, and a password of at least 16
characters. Then run:

```bash
docker compose --env-file .env -f compose.yaml up --build --detach
```

The dashboard listens on `http://127.0.0.1:8787/admin` by default and uses HTTP Basic
authentication. It binds only to loopback; use an authenticated HTTPS reverse proxy or SSH tunnel
if it must be accessed from another machine.

On a Linux host with Docker Compose and systemd, the setup script can create and start a boot-time
service. Supply secrets through environment variables so they are not placed in shell arguments:

```bash
export CLOUDFLARE_ACCOUNT_ID='...'
export CLOUDFLARE_API_TOKEN='...'
export D1_DATABASE_ID='...'
export ADMIN_EMAIL='developer@example.com'
./scripts/setup-feedback-dashboard-daemon.sh
```

The script creates a mode-`0600` `.env` file and generates the dashboard password. Rebuild and
restart the daemon after changing the checkout with:

```bash
./scripts/update-feedback-dashboard-daemon.sh
```

Pass `--pull-source` to perform a fast-forward-only `git pull` before rebuilding.

## Deployment

1. Create the database with `npx wrangler d1 create practice-takes-feedback`.
2. Copy `wrangler.example.jsonc` to the ignored local file `wrangler.jsonc`, then copy the
   database ID into it. Do not commit account IDs, tokens, or secret values.
3. Set `ADMIN_EMAILS` in `wrangler.jsonc` to a comma-separated allowlist of administrator email
   addresses. This is an additional application check, not a replacement for Access.
4. Apply migrations with `npm run db:migrate:remote`.
5. Run `npm run deploy` once to create the Worker. With `workers_dev` disabled and no custom
   route configured yet, this initial deployment is not publicly reachable.
6. Generate and configure the production signing key:
   `openssl rand -hex 32 | npx wrangler secret put SUBMISSION_SIGNING_KEY`.
7. Confirm it with `npx wrangler secret list`, then configure a custom HTTPS route in the
   Cloudflare dashboard.
8. Create a Cloudflare Access self-hosted application covering `/admin` and `/v1/admin/*` on that
   route. Its allow policy must contain the same people configured in `ADMIN_EMAILS`. Verify an
   unauthenticated request receives an Access login or denial before sharing the URL.

`workers_dev` is disabled so deployment requires an intentional route. Cloudflare API credentials
used for deployment should be scoped to this Worker and D1 database. The runtime receives only the
application configuration, `FEEDBACK_DB` binding, and signing secret; it does not receive
account-level administration tokens.

## Submission protocol

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

## Testing

Run the complete service test suite from the `services` directory:

```bash
npm install
npm test
```

The suite covers receipts, validation, expiration, replay protection, request-size limits, rate
limiting, HTTPS enforcement, administrative authentication, filtering, triage updates, duplicate
linking, and CSV export.

## Security and abuse controls

- 1.5 MiB request limit with an independently bounded optional PNG or JPEG screenshot
- Screenshots require explicit per-submission consent, contain visible Practice Takes windows,
  and explicitly exclude the feedback form itself
- configurable minimum supported semantic application version
- timestamps constrained to a ten-minute window
- five-minute HMAC authorizations with unique token IDs
- one-use token storage enforced by a database uniqueness constraint
- per-client authorization limits and per-client/per-installation submission limits

Rate-limit defaults live in `wrangler.jsonc`. Cloudflare WAF rules may add an outer IP limit without
changing the application protocol.
