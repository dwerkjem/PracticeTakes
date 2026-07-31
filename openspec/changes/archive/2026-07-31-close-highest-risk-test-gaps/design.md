## Context

`services/feedback-intake` is a Cloudflare Worker backed by D1, which is
SQLite. Its schema lives in seven ordered files under `migrations/`, applied in
production by `wrangler d1 migrations apply`. Its Vitest suites, however, never
touch SQLite or those migrations: three separate in-memory fakes
(`MemoryD1`, `AdminD1`, `NotificationDatabase`) implement `prepare().bind()`
and then branch on substrings of the SQL text to decide what to return. The
suites therefore verify the worker's *control flow* while assuming its SQL is
correct and its schema matches.

The D1 surface the worker actually uses is small: `prepare`, `bind`, `first`,
`all`, `run`, and one `batch`. `operations.ts` reads `result.meta.changes`, and
`index.ts` relies on a `UNIQUE` constraint violation surfacing as a catchable
error (`isUniqueConstraintError`). That is a small enough surface to bridge to
a real SQLite engine.

On the Python side, `scripts/` is 3,651 lines with a single `unittest`-based
test file that no workflow and no pre-commit hook invokes. There is no
tracked Python dependency manifest for `scripts/`, and the pre-commit hooks
call the scripts directly with `python3`.

## Goals / Non-Goals

**Goals:**
- Worker tests execute real SQL against a schema built from the project's own
  migration files, so invalid SQL and schema drift fail in CI.
- One shared test database helper replaces three divergent fakes.
- The existing suites keep their current assertions; only the database
  underneath changes.
- The Python test suite runs in CI on every pull request touching `scripts/`.
- `scripts/release/version.py`, which drives the release workflow, gets tests.

**Non-Goals:**
- Coverage instrumentation or thresholds for any language.
- Tests for `docker-server.ts`, `dashboard.js`, `audit.js`, or the
  `practice_takes_roadmap` package.
- Running the worker inside `workerd` (see decision 2).
- C++ test gaps, sanitizer builds, macOS `ctest`, or feedback wire-contract
  conformance tests.
- Changing any migration, any production query, or the `services-check.yml`
  workflow.

## Decisions

**1. Back the test database with `node:sqlite` rather than `better-sqlite3`.**
`node:sqlite` is a Node built-in, so it adds nothing to
`services/package-lock.json` and requires no native compile step in CI.
The alternative, `better-sqlite3`, is more mature but is a native module: it
needs a build toolchain on every platform a contributor develops on, and it
would be the service's first native dependency. CI already pins Node 26, where
`node:sqlite` is available; on older local Node versions it emits an
experimental warning but functions. If that warning proves disruptive, swapping
the adapter's engine is a change to one file.

**2. Do not adopt `@cloudflare/vitest-pool-workers`.**
Running the suites inside `workerd` with Miniflare's D1 would be the most
faithful option and was the main alternative considered. It is rejected for
this change because it replaces the whole test runtime — the repository's
custom Vite plugin for `.html`/`.css`/`.js` text imports
(`services/vitest.config.mts`) would need reworking, `admin.test.ts` and
`notifications.test.ts` call their handlers directly rather than through
`fetch`, and `docker-server.ts` targets Node rather than `workerd`. That is a
larger, riskier change than the defect it closes requires. The gap being fixed
here is "the SQL is never executed", not "the runtime is not `workerd`", and
D1 is SQLite underneath. Adopting the pool remains a reasonable follow-up.

**3. Apply the real migration files; never hand-write the test schema.**
The helper reads `migrations/*.sql`, sorts by filename, and executes each in
order against a fresh in-memory database. A hand-maintained `schema.sql` for
tests would reintroduce exactly the drift this change exists to eliminate. The
numeric filename prefixes make lexicographic order the correct order, and a
test asserts the discovered file list is non-empty and contiguous so a
mis-numbered or unreadable migration fails loudly rather than silently
producing a partial schema.

**4. One database per test, created fresh.**
Each test constructs its own in-memory database and applies the migrations.
This keeps tests independent with no teardown or truncation logic. Applying
seven small DDL files to an in-memory SQLite database is sub-millisecond, so
per-test setup is not a meaningful cost at this suite size.

**5. The adapter implements only the D1 surface the worker uses.**
`prepare`, `bind`, `first`, `all`, `run`, and `batch`, returning D1-shaped
results (`{ results, success, meta }`) with `meta.changes` populated from
SQLite's change count, and `batch` wrapped in a transaction to match D1's
atomicity. Methods the worker does not call are left unimplemented and throw a
clear error, so a future query that needs one fails visibly instead of silently
returning a wrong shape. The adapter is typed as `D1Database` at its boundary
via the existing `as unknown as D1Database` pattern already used in the suites.

**6. Seed test data through SQL, not through fake object fields.**
The current fakes are seeded by pushing objects onto arrays. With a real
database, helpers insert rows with `INSERT` statements. This is what surfaces
schema drift in the *tests themselves*: a test that seeds a column the
migrations do not define now fails.

**7. Run Python tests with the standard library, via a small path-based loader.**
The existing test file is already `unittest`-based, `scripts/` has no tracked
dependency manifest, and the pre-commit hooks invoke `python3` directly, so
pytest would mean introducing dependency management for `scripts/` — worth
doing when the suite needs fixtures and parametrisation, not now.

`python -m unittest discover` cannot be the runner, though. Implementation
found that `discover -s scripts -t .` fails outright ("Start directory is not
importable") and `discover -s scripts` **reports `NO TESTS RAN` and exits
zero** — the script directories are not importable packages, so a CI job wired
to it would stay green while executing nothing. Making them importable is not
an option either: `scripts/secrets` would shadow the standard library's
`secrets` module.

`scripts/run_tests.py` therefore locates `test_*.py` files by path and imports
each under a unique synthetic module name. It is standard-library only, keeps
"a test added anywhere under `scripts/` runs with no workflow change" true, and
treats an empty result as an error rather than a silent pass.

**8. Give the Python tests their own workflow, scoped to `scripts/**`, on the
runner's Python.**
`services-check.yml` is Node-only and path-scoped to `services/**`; folding a
Python job into it would run Node setup for Python-only changes and vice
versa. A separate `python-check.yml` keeps each gate's trigger honest, matching
how `cpp-quality-check.yml` and `services-check.yml` are already split.

The job uses the runner's preinstalled `python3` rather than a pinned
`actions/setup-python`. No existing workflow uses that action — `release.yml`,
`secret-scan.yml`, `benchmarks.yml`, and both clang-tidy workflows all invoke
these same scripts with the runner's interpreter — and the suite imports
nothing outside the standard library, so adding a pinned action would widen the
supply-chain surface for no gain.

## Risks / Trade-offs

- **[Risk]** Porting the suites onto real SQL surfaces pre-existing defects in
  the worker's queries, expanding the change beyond test code. → This is the
  intended outcome. Fix genuine defects in this change; if one turns out to be
  large, land the test that exposes it as a documented failing case in a
  follow-up change rather than weakening the test to pass.
- **[Risk]** `node:sqlite` is marked experimental on Node versions before 26
  and emits a warning locally. → CI pins Node 26. The warning is cosmetic, and
  decision 1 keeps the engine swappable from a single file.
- **[Risk]** SQLite and D1 are not perfectly identical (D1 adds its own limits,
  result metadata, and error text). → The adapter normalises result shape and
  `meta.changes`; `isUniqueConstraintError` must be verified against SQLite's
  actual error text, and that check is an explicit task. Remaining divergence
  is far smaller than the current gap, where no SQL runs at all.
- **[Trade-off]** Tests become slower than array-manipulating fakes. At this
  suite size the difference is negligible, and the current speed is bought by
  not testing the database.
- **[Risk]** Enabling a new required Python check could block unrelated pull
  requests if the existing secrets tests turn out to be stale. → Task 3.1 runs
  them locally before the workflow is added; if they fail, they are fixed as
  part of this change.

## Migration Plan

No production migration. The rollout is: land the adapter and the
`migrations.test.ts` integrity test first (additive, nothing removed), then
port one suite at a time so a regression is attributable to a single suite,
deleting each fake as its suite is ported. The Python workflow lands last and
independently. Rollback for either half is reverting the commits; no runtime,
schema, or deployment artefact is touched.

## Resolved Questions

- **Should `python-check.yml` also run on `push` to `main`?** Yes — it matches
  `services-check.yml` on both `pull_request` and `push` to `main`.
- **Should the migration-integrity test assert the resulting schema contains
  every table the worker queries?** Yes. Reading `sqlite_master` after applying
  the migrations turned out to be trivial, so the test pins the table set
  directly rather than parsing SQL out of `src/`.
