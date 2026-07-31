## Why

Two areas of the codebase can ship a defect that no test can catch.

**The feedback worker never executes SQL.** `services/feedback-intake` has four
Vitest suites with good behavioural depth, but the three that touch the
database run against hand-written fakes — `MemoryD1` in `index.test.ts`,
`AdminD1` in `admin.test.ts`, `NotificationDatabase` in
`notifications.test.ts` — that dispatch on SQL *substring* matching
(`sql.includes("FROM authorization_requests")` and similar). No statement is
ever parsed or executed, and the seven files under `migrations/` are never
applied in any test. A misspelled column, a SQL syntax error, a query against a
table a migration never created, or drift between a migration and the code that
reads it all pass CI and fail in production against real D1. The idempotent
submission path is the sharpest example: it depends on a `UNIQUE` constraint
added in `0005_idempotent_submissions.sql` and on catching the resulting
constraint error, and no test exercises the constraint that makes it work.
Three divergent fakes also mean each new query must be hand-taught to whichever
fake the suite uses, so the cost of writing a test grows with every one added.

**The Python scripts have one test file and nothing runs it.**
`scripts/` holds 3,651 lines that gate releases, encrypt secrets, and mutate
persisted roadmap state. `scripts/secrets/test_secrets_manager.py` (194 lines)
exists but is referenced by no workflow and no pre-commit hook, so it has been
dead since it was written. `scripts/release/version.py` computes and writes the
version that `release.yml` tags and publishes, with no test at all.

## What Changes

- Replace the three hand-written fakes with one test-only D1 adapter backed by
  a real in-memory SQLite database (`node:sqlite`), seeded by applying every
  file in `services/feedback-intake/migrations/` in filename order.
- Port the three database-backed Vitest suites onto the adapter. Their
  assertions stay as they are — only the database underneath them changes — so
  this is a fidelity change, not a rewrite of what is asserted.
  `access.test.ts` is JWT-only and is untouched.
- Add a migration-integrity test that applies the full migration set to an empty
  database and fails if any migration errors or is applied out of order.
- Add a CI job that runs the Python test suite via stdlib `unittest` discovery
  over `scripts/`, blocking merge on failure. No new Python dependency: the
  existing tests are already `unittest`-based.
- Add tests for `scripts/release/version.py` covering parsing, bump arithmetic,
  and the `VERSION` file round-trip.

Not in scope, deliberately: coverage instrumentation for any language,
`docker-server.ts` and browser-asset tests, tests for the
`practice_takes_roadmap` package, C++ gaps (`AudioInputService`, the analysis
components), feedback wire-contract conformance tests, sanitizer builds, and
macOS `ctest`. Each is tracked as a follow-up area in
`docs/development/QA_STRATEGY.md` and should become its own change.

## Capabilities

### New Capabilities
- `worker-database-fidelity`: feedback-intake tests execute real SQL against a
  database built from the project's own migration files, so schema drift and
  invalid SQL fail in CI rather than in production.
- `python-script-test-gate`: the repository's Python test suite runs in CI on
  every pull request that touches `scripts/`, and a failing test blocks merge.

### Modified Capabilities
<!-- None. The archived `services-quality-gate` capability was never synced into
     openspec/specs/, and this change does not alter its requirements: the
     existing `npm run test` invocation is unchanged, only what those tests run
     against. -->

## Impact

- **`services/feedback-intake/test/`** — new `support/d1.ts` adapter and
  `migrations.test.ts`; the `MemoryD1` class and its statement dispatcher are
  deleted from `index.test.ts`; the four existing suites switch to the shared
  helper.
- **`services/feedback-intake/migrations/`** — read by tests for the first
  time; no migration content changes.
- **`.github/workflows/`** — new `python-check.yml` scoped to `scripts/**`.
  `services-check.yml` is unchanged; the new worker tests run inside the
  `npm run test` step it already invokes.
- **`scripts/`** — new `scripts/release/test_version.py`.
- **Runtime** — Vitest gains a real SQLite engine per test database.
  `node:sqlite` is a Node built-in, so no dependency is added to
  `services/package-lock.json`; CI already pins Node 26.
- **Expected fallout** — porting the suites onto real SQL may surface genuine
  defects that the fake was masking. That is the point of the change; any such
  failure is fixed in this change or split out with an explicit note.
