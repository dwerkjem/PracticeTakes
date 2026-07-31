# QA strategy

This is a living plan for closing quality-assurance gaps identified during a
July 2026 review of the project's CI/CD, testing, and repository hygiene. It
complements the day-to-day mechanics already documented in
[Code quality](QUALITY.md) (formatting/linting) and
[Design and architecture review checklist](ARCHITECTURE_QA.md) (PR review
criteria) — this document is about *what to build next* and in what order.

Each numbered area below is meant to become its own OpenSpec change
(`openspec new change <name>`) when work on it starts, rather than one large
change. Sequencing and dependencies between areas are called out where they
exist.

## Current state (baseline)

> This section is the July 2026 snapshot that produced areas 1–6. It is left as
> written; work completed since then is recorded in the areas below and in
> [area 7](#7-database-backed-service-tests-and-python-test-execution).

- **C++ formatting/linting**: `clang-format` on every local commit
  (pre-commit), `clang-tidy` full-repo check on every pull request
  ([cpp-quality-check.yml](../../.github/workflows/cpp-quality-check.yml)),
  and an auto-fix pass on `main`
  ([clang-tidy-main.yml](../../.github/workflows/clang-tidy-main.yml)).
- **C++ tests**: Catch2 (`PracticeTakesTests`), run via `ctest` only on the
  `Linux x64` leg of [build-multiplatform.yml](../../.github/workflows/build-multiplatform.yml)
  (`run_tests: true`). Linux arm64, Windows x64/arm64, and macOS legs build
  but never execute tests.
- **TypeScript service** (`services/feedback-intake`, a Cloudflare Worker):
  has `tsc --noEmit` (`npm run check`) and `vitest` (`npm run test`,
  4 suites) defined, and a workspace-level aggregator at
  `services/package.json`, but **no GitHub Actions workflow runs either one**.
- **Security scanning**: none (no CodeQL or equivalent for C++ or TS).
- **Dependency hygiene**: no Dependabot/Renovate config. GitHub Actions are
  pinned by mutable tag (`@v6`, `@v4`, `@v5`), not commit SHA. JUCE and
  Catch2 are pinned by immutable git tag in `CMakeLists.txt` (already good).
- **Performance**: one Catch2 benchmark exists today
  (`tests/features/analysis/tuner/PitchDetectorTests.cpp`, tag `[.benchmark]`, comparing FFT vs.
  scalar autocorrelation) but it is never executed in CI, has no other
  pipeline covered (spectrogram FFT, harmonic analyzer, audio FIFO), and has
  no baseline/regression comparison.
- **Repo governance**: no `SECURITY.md`, `CONTRIBUTING.md`, `CODEOWNERS`, or
  issue/PR templates.

## 1. TypeScript service CI

**Problem**: `services/feedback-intake` can merge with a failing type-check
or failing test and nothing blocks it.

**Plan**: Add a workflow (e.g. `.github/workflows/services-check.yml`)
triggered on `pull_request`/`push` for changes under `services/**`, running
`npm ci` then `npm run check` and `npm run test` from `services/` (the
existing `--workspaces --if-present` aggregator already fans out to
`feedback-intake` and any future service without editing the workflow).

**Dependencies**: none. Independent of the other items — good first pick.

## 2. Cross-platform test execution

**Problem**: `PracticeTakesTests` only runs on `Linux x64` in
`build-multiplatform.yml`; a regression specific to another architecture or
compiler (MSVC UB, endianness, ARM alignment) ships silently.

**Plan**: Flip `run_tests: true` for the Linux arm64 and Windows legs first
(same `ctest` invocation already wired in, just currently gated off);
evaluate macOS separately since it may need its own runner-specific
`ctest`/JUCE audio-backend handling. Do this per-leg, verifying each
platform's `ctest` run is clean before enabling it, rather than flipping all
flags in one change.

**Dependencies**: none, but do this before leaning harder on the performance
suite (item 6) if that suite also runs per-platform — a flaky non-Linux test
run would then block two things at once.

## 3. Dependency and supply-chain hygiene

**Problem**: No automated notice of outdated/vulnerable GitHub Actions or npm
packages; Actions are pinned by mutable tag, so a compromised upstream tag
move is silently pulled in on the next run.

**Plan**:
- Add a Dependabot config (`.github/dependabot.yml`) covering `github-actions`
  and `npm` (scoped to `services/feedback-intake`) ecosystems.
- Re-pin existing `actions/*@vN` references to commit SHAs (keep the version
  as a trailing comment, e.g. `actions/checkout@<sha> # v6.x.x`), matching
  common OpenSSF/OWASP supply-chain guidance for CI.
- Leave JUCE/Catch2 `FetchContent` pins as-is (already immutable git tags);
  Dependabot doesn't cover CMake `FetchContent`, so bumping those stays a
  manual, deliberate decision.

**Dependencies**: none.

## 4. Security scanning (CodeQL)

**Problem**: No static security analysis beyond `clang-tidy`'s
`bugprone-*`/`clang-analyzer-*` checks (which are correctness/bug-focused,
not security-focused) and nothing at all for the TypeScript service.

**Plan**: Add `github/codeql-action`-based scanning for both languages
(`cpp` and `javascript-typescript`), scheduled weekly plus on `pull_request`.
The C++ analysis needs a build step (CodeQL's C++ extractor traces the
actual compile), so it will reuse the same configure/build steps already
used by `cpp-quality-check.yml`.

**Dependencies**: benefits from item 1 (TS CI) existing first, so there's
already a place to slot a TypeScript build/install step for CodeQL to hook
into — not a hard blocker, just convenient sequencing.

## 5. Repo governance documentation

**Problem**: No documented vulnerability-disclosure process, contribution
guidelines, code-ownership map, or issue/PR templates.

**Plan**: Add `SECURITY.md` (how to privately report a vulnerability),
`CONTRIBUTING.md` (pointing at `docs/development/`, which already covers
build/style/quality in depth), `CODEOWNERS` (if/when there's more than one
maintainer), and `.github/ISSUE_TEMPLATE/`/`PULL_REQUEST_TEMPLATE.md`.

**Dependencies**: none. Lowest-risk, can be done any time, including
alongside other items.

## 6. Performance monitoring and impact suite

**Problem**: Real-time audio code has hard latency/allocation constraints
(see [Architecture § Audio-thread boundary](ARCHITECTURE.md#audio-thread-boundary)
and [Code style § Real-time audio rules](CODE_STYLE.md#real-time-audio-rules)),
but nothing measures whether a change quietly regresses the analysis
pipelines' performance. One Catch2 benchmark exists
(`tests/features/analysis/tuner/PitchDetectorTests.cpp`, `[.benchmark]` tag) but it's never run in
CI and there's no comparable coverage for the spectrogram FFT path, harmonic
analyzer, or `AudioSampleFifo` push/pop under load.

**Plan**:
- **Expand benchmark coverage**: add `[.benchmark]`-tagged Catch2 cases for
  the spectrogram's per-frame FFT+draw path, `HarmonicAnalyzer::analyze`,
  and `AudioSampleFifo` push/pop at realistic block sizes — the same
  hot paths already called out in `ARCHITECTURE.md`'s pipeline sections.
- **Run benchmarks in CI, but keep them informational, not PR-blocking at
  first.** CI runner performance is noisy (shared vCPUs, no fixed clock),
  so a hard pass/fail threshold on absolute time is likely to be flaky.
  Start by publishing results (e.g. Catch2's `--reporter JSON`
  or console output, uploaded as a build artifact or step summary) on a
  schedule and on `push` to `main`, not on every PR.
- **Track a baseline and flag regressions once noise is characterized.**
  After a few weeks of collected data, decide on a realistic
  regression threshold (e.g. "flag if a benchmark is >2x its trailing
  median," not "must beat commit N-1") and only then consider gating PRs —
  gate on relative regression against a rolling baseline, never on a fixed
  absolute number.
- **Do not run benchmarks on every platform.** Pick one consistent runner
  (e.g. `ubuntu-24.04`, matching the one already running functional tests)
  as the benchmark baseline machine; cross-platform performance comparison
  is a separate, later concern from catching same-platform regressions.

**Dependencies**: benefits from item 2 (cross-platform test execution)
settling first so "which platform runs what" isn't being decided twice at
once, but isn't strictly blocked by it.

## 7. Database-backed service tests and Python test execution

**Status**: done (2026-07-30, `close-highest-risk-test-gaps`).

**Problem**: the two places a defect could ship with no test able to catch it.
The feedback worker's suites ran against three hand-written fakes that
dispatched on SQL *substring* matching, so no statement was ever parsed or
executed and the seven files under `migrations/` were never applied — invalid
SQL and schema drift were invisible. Separately, `scripts/` held 3,651 lines
with one test file that no workflow and no pre-commit hook ever ran.

**Outcome**:
- `services/feedback-intake/test/support/database.ts` builds an in-memory
  `node:sqlite` database from the real migration files and exposes the D1
  surface the worker uses. All three fakes are gone.
- `test/migrations.test.ts` applies the migration set and names any file that
  fails.
- `scripts/run_tests.py` plus `.github/workflows/python-check.yml` run the
  Python suite on every pull request touching `scripts/`, and
  `scripts/release/version.py` — which the release workflow depends on — now
  has tests.

## Follow-up test-coverage areas

These were found during the same 2026-07-30 coverage review and deliberately
left out of area 7. Each should become its own OpenSpec change.

### 8. Coverage measurement

**Status**: done. Coverage is measured for all three languages and published as
an informational CI artifact plus a step summary by
`.github/workflows/coverage.yml`. No threshold gates anything, deliberately —
gating is a separate decision to make once the numbers have been watched.

Run it locally with `python3 scripts/quality/run_coverage.py` (C++),
`npm run test:coverage` from `services/`, and
`python3 scripts/quality/run_python_coverage.py`.

#### Measured baseline

Taken at `efb65ec7b5fb` on 2026-07-31. These replace the hand-counted estimates
that used to be in this document.

| Language | Lines | Notes |
|---|---|---|
| C++ | **89.7%** (3081/3434) | Over *instrumented* sources only — see the caveat below |
| TypeScript | **76.2%** (499/655) | 72.8% statements, 67.2% branches, 6 files |
| Python | **29%** (584/2025) | `practice_takes_roadmap` is 0%, `secrets_manager.py` 40% |

**The C++ figure needs its caveat attached every time it is quoted.** 89.7% is
measured over the 25 translation units compiled into `PracticeTakesTests` — out
of 43 under `src/`. The other 18 are not instrumented at all, so they are absent
from both the numerator *and* the denominator. A tool cannot report on code it
was never given.

That is why the report has a second half: `scripts/quality/coverage_sources.py`
lists what is outside the test build, and CI publishes it beside the percentage.
Read them together or the number means very little.

The same trap exists in TypeScript and was hit while setting this up: with a
mistaken `include` pattern, `docker-server.ts` was missing from the report
rather than showing as 0%, and the headline read 82% instead of 72.8%. The
config now sets `all: true` with a corrected pattern.

### 9. C++ code outside the test build

**Problem** (measured at `efb65ec7b5fb`, 2026-07-31): `PracticeTakesTests`
compiles **25 of 43 translation units** under `src/` — 58.1%. The remaining 18
never enter the test binary, so no coverage tool can say anything about them.
Run `python3 scripts/quality/coverage_sources.py` for the current list; CI
publishes it with every coverage run.

The earlier hand count of "64 of 99 source files" included headers, which are
not separately compiled units; 25/43 is the figure that matters for coverage,
and it is now measured rather than estimated. The largest untested units are
`AudioInputService.cpp` (device lifecycle, gain, level metering, dropout
accounting), `FeedbackComponent.cpp`, `TunerComponent`/`TunerDrawing`,
`SpectrogramComponent`, and the `MainComponent*` shell files.

**Plan**: extract the testable logic out of the JUCE components — the pattern
the workspace model and settings codecs already follow — rather than trying to
instantiate components headlessly. Start with `AudioInputService`, which is the
highest-value target and closest to pure logic already.

### 10. Untested worker paths

**Problem**: `docker-server.ts` (165 lines, including the `authorized()` bearer
check and the `sameOriginRequest()` CSRF guard — the self-hosted deployment
path), the `scheduled` cron handler in `index.ts`, and the browser assets
`dashboard.js`/`audit.js` (431 lines) have no tests. Area 7's database helper
now makes the first two straightforward.

### 11. Roadmap tooling tests

**Problem**: `scripts/practice_takes_roadmap` is 2,046 lines that call the
GitHub API and mutate persisted state, with no tests. Area 7 established where
Python tests live and how CI runs them; this fills in the largest gap.

### 12. Feedback wire-contract conformance

**Problem**: `contracts/feedback/v1.schema.json` is documentation only. The C++
client hand-builds the JSON, the worker hand-validates it, and no test on
either side checks either against the schema, so the two can drift apart while
both remain internally consistent.

### 13. Concurrency and real-time verification

**Problem**: `AudioSampleFifo` is a lock-free single-producer/single-consumer
ring, and every one of its tests is single-threaded. There is no ASan/UBSan/TSan
build in CI, and the audio-thread no-allocation rules asserted in
[Audio-thread safety](performance-audio-thread-safety.md) are unverified.

**Plan**: add a sanitizer build to CI plus genuinely concurrent producer and
consumer tests for the FIFO.

### 14. macOS test execution

**Problem**: both macOS legs of `build-multiplatform.yml` still set
`run_tests: false` — the deferred remainder of area 2.

## Suggested sequencing

1. TypeScript service CI (independent, immediate value, no risk of flakiness)
2. Repo governance docs (independent, can run in parallel with anything)
3. Dependency/supply-chain hygiene (independent)
4. Cross-platform test execution (flip one platform at a time)
5. Performance monitoring and impact suite (informational first, gating later)
6. Security scanning / CodeQL (benefits from TS CI existing, otherwise last
   because it has the most setup surface: two languages, build integration)

## Explicitly out of scope for this round

- **macOS code signing/notarization** (currently ad-hoc `codesign --sign -`).
  Flagged in the original gap analysis but requires obtaining an Apple
  Developer ID certificate — an account/cost decision, not a QA-process
  decision — so it's deliberately not scheduled here.
