## 1. Shared test database helper

- [x] 1.1 Add `services/feedback-intake/test/support/database.ts` exporting a
      helper that creates an in-memory `node:sqlite` database, reads every
      `*.sql` file under `migrations/`, sorts by filename, and applies each in
      order.
- [x] 1.2 In the same module, implement the D1 adapter over that database:
      `prepare`, `bind`, `first`, `all`, `run`, and `batch`, returning
      D1-shaped results (`{ results, success, meta }`) with `meta.changes`
      populated from SQLite's change count.
- [x] 1.3 Wrap `batch` in a transaction so a failure partway through leaves no
      rows behind, and make unimplemented D1 methods throw a named error rather
      than returning a wrong shape.
- [x] 1.4 Verify `isUniqueConstraintError` in `src/index.ts` recognises the
      error `node:sqlite` raises for a `UNIQUE` violation; if it does not,
      widen the check and note it as a real defect the fakes were masking.
- [x] 1.5 Add SQL-based seeding helpers for the rows the suites currently
      construct as plain objects (submissions, authorization requests,
      admin receipts, notification counters).

## 2. Migration integrity test

- [x] 2.1 Add `services/feedback-intake/test/migrations.test.ts` that applies
      the full migration set to an empty database and fails if any file raises,
      naming the offending file in the failure message.
- [x] 2.2 Assert the discovered migration list is non-empty and that filename
      prefixes are contiguous, so a mis-numbered or unreadable migration fails
      loudly instead of producing a partial schema.
- [x] 2.3 Confirm the test fails as intended by temporarily corrupting a
      migration locally, then revert.

## 3. Port the database-backed suites

- [x] 3.1 Port `test/notifications.test.ts` to the helper and delete
      `NotificationDatabase`. Smallest surface — do it first to shake out
      adapter gaps.
- [x] 3.2 Port `test/admin.test.ts` to the helper and delete `AdminD1`,
      including the `/v1/admin/operations` and retention routes that reach
      `operations.ts`.
- [x] 3.3 Port `test/index.test.ts` to the helper and delete `MemoryD1` and its
      statement dispatcher.
- [x] 3.4 Add a test for the idempotent-submission path that stores the same
      submission twice and asserts the real `UNIQUE` constraint from
      `0005_idempotent_submissions.sql` drives duplicate detection and returns
      the original receipt.
- [x] 3.5 Add a test asserting a failed batch leaves no partial rows.
- [x] 3.6 Confirm every assertion that existed before the port still exists and
      passes; record any assertion that had to change and why.
- [x] 3.7 Triage any genuine worker defect the port surfaces: fix it here, or
      if it is large, capture it explicitly in the change summary rather than
      weakening the test.
- [x] 3.8 Run `npm run check` and `npm run test` from `services/` and confirm
      both are clean.

## 4. Python test execution in CI

- [x] 4.1 Run `python3 -m unittest discover -s scripts -p "test_*.py" -t .`
      locally and fix any failure in `scripts/secrets/test_secrets_manager.py`
      that has accumulated while nothing ran it.
- [x] 4.2 Add `.github/workflows/python-check.yml` triggered on
      `pull_request` and on `push` to `main`, path-scoped to `scripts/**` and
      the workflow file itself, running the suite via `scripts/run_tests.py`
      on the runner's preinstalled Python. (Deviation: `unittest discover`
      silently runs zero tests over this tree, and no existing workflow uses
      `actions/setup-python` — see design decisions 7 and 8.)
- [x] 4.3 Confirm the workflow does not trigger for a change touching only
      `src/**` or `services/**`.

## 5. Release version tests

- [x] 5.1 Add `scripts/release/test_version.py` covering `parse_version`,
      `format_version`, and `calculate_next` for major, minor, and patch bumps.
- [x] 5.2 Cover malformed version strings and assert they are rejected.
- [x] 5.3 Cover `read_version`/`write_version` round-tripping against a
      temporary `VERSION` file, without mutating the repository's own.

## 6. Documentation and verification

- [x] 6.1 Update `services/feedback-intake/README.md` to describe how tests get
      their database and that the schema comes from `migrations/`.
- [x] 6.2 Update `docs/development/QA_STRATEGY.md`: mark this area done and
      record the deferred areas as numbered follow-ups — coverage
      instrumentation, `docker-server.ts` and browser-asset tests,
      `practice_takes_roadmap` tests, `AudioInputService` and analysis-component
      tests, feedback wire-contract conformance, sanitizer builds, macOS
      `ctest`.
- [x] 6.3 Confirm the full diff touches no migration, no production query, and
      no existing workflow file.
- [ ] 6.4 Confirm both new checks pass on a pushed pull request (live CI run).
      Blocked: needs a push to a branch, which is the maintainer's call. Both
      suites and the type-check pass locally (65 service tests, 30 Python
      tests, `tsc --noEmit` clean).
