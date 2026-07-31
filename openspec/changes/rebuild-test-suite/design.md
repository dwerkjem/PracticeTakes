## Context

Practice Takes has 288 Catch2 unit tests, all of one shape: single-threaded,
in-process, asserting against pure logic that was deliberately split out of the
JUCE components. That shape is the repository's existing testability pattern and
it works — but it is the only shape present, and it has a measured blind spot.

`docs/development/QA_STRATEGY.md` area 9 counted, by hand, that
`PracticeTakesTests` compiles 64 of 99 source files and that roughly 9,000 of
16,100 lines never enter the test binary. The untested files are the
`Component`-heavy ones: `AudioInputService.cpp`, `FeedbackComponent.cpp`,
`TunerComponent`/`TunerDrawing`, `SpectrogramComponent`, and the
`MainComponent*` shell. Area 8 records that nothing measures coverage in any
language, which is why those figures had to be counted by hand in the first
place.

What already exists and constrains the design:

- **`tests/` mirrors `src/`** as of the commit this change builds on, with
  `tests/support/` exempt and both roots on the test target's include path.
- **`scripts/quality/ui-validation/`** is a working X11 harness: a zsh driver
  plus three small C++ tools (`xwindow_capture`, `window_control`,
  `pointer_control`) that launch `build/bin/PracticeTakes`, manipulate its
  window, and capture screenshots. It is local-only and oriented at golden-image
  comparison, and it is compiled ad hoc with `/usr/bin/g++`.
- **`[.benchmark]`** is the established convention for opt-in slow tests, and
  `benchmarks.yml` already runs them on `main` and writes a step summary.
- **CI is GCC on Ubuntu runners** for the Linux legs, with `ccache` and no
  explicit compiler pinning. `ctest` runs on the Linux and Windows legs; both
  macOS legs still set `run_tests: false`.
- **No Xvfb anywhere** in the repository, and no CI job launches the built
  application.
- **`AudioSampleFifo`** is a lock-free SPSC ring whose every existing test is
  single-threaded (area 13).

## Goals / Non-Goals

**Goals:**

- Replace hand-counted coverage estimates with measured, published figures in
  all three languages.
- Make the untested third of `src/` visible as a list rather than a statistic,
  including files that are absent from the test target entirely.
- Capture what is currently verified only in the maintainer's head as a
  reviewable checklist, with recorded runs.
- Prove the application actually starts, opens a tool, and shuts down — the one
  thing 288 unit tests cannot tell us.
- Verify the SPSC FIFO's concurrency claim with genuinely concurrent tests.
- Keep the default test run fast enough to run before every commit.

**Non-Goals:**

- Coverage thresholds or gating of any kind. Baseline first.
- Closing the coverage gap by extracting logic out of the JUCE components
  (area 9). This change measures; closing is per-component follow-up work.
- Sanitizer builds in CI (area 13), though the concurrent tests here are
  weaker without TSan and that is called out below.
- Golden-image or pixel comparison. The existing ui-golden harness stays as it
  is; this change borrows its X11 tools, not its goldens.
- macOS test execution (area 14), worker paths (area 10), roadmap tooling
  (area 11), contract conformance (area 12).

## Decisions

### Decision 1 — gcov plus gcovr for C++ coverage

CI's Linux legs build with GCC, so `--coverage` (gcov) is the native fit and
`gcovr` produces both a human summary and machine-readable XML/JSON in one
invocation without a separate profile-merge step. `llvm-cov` would mean pinning
Clang for the coverage job specifically, adding a second toolchain to a build
that currently does not name a compiler at all.

Coverage is a separate CMake option (default off) and a separate build tree.
Instrumented objects are not interchangeable with ordinary ones, and `ccache`
interacts badly with coverage counters, so trying to reuse the normal tree would
be a false economy.

**Alternative considered:** running coverage only on a schedule rather than per
PR. Rejected for now — a figure nobody sees on their own PR is a figure nobody
acts on. It stays cheap because only one leg does it.

### Decision 2 — the coverage denominator includes files outside the test target

The single most misleading thing this change could produce is a healthy
percentage computed only over files that happen to be in
`add_executable(PracticeTakesTests ...)`. Today that would report on 64 files
and silently ignore 35.

So the report is assembled in two parts: gcovr's measurement of instrumented
files, plus a list derived by comparing the `src/` tree against the test
target's source list. Files in the second list are reported as *not built into
the test binary* — a distinct and more serious category than 0% coverage, and
the one that actually names area 9's backlog.

### Decision 3 — Xvfb for headless launch, reusing the existing X11 tools

JUCE on Linux needs a real X server; there is no offscreen mode that exercises
the same window path. Xvfb is the standard answer, is a single apt package, and
is what the existing ui-validation tools already implicitly assume (they speak
Xlib).

The smoke tests therefore reuse `xwindow_capture` and `window_control` rather
than growing a second X11 helper set. What is new is the driver: the existing
`run-ui-golden.zsh` is a golden-image workflow that writes evidence for a human
to inspect, whereas a smoke test must assert and exit non-zero. The two share
tools and share nothing else.

**Alternative considered:** driving the app through an added test-only IPC or
scripting hook. Rejected — it would mean shipping a control surface in the
application to test the application, and the X11 route needs nothing in `src/`.

### Decision 4 — smoke tests are a script, not a Catch2 target

A smoke test launches a separate process, waits on a window, and kills it on
timeout. Catch2 is a poor fit for that: the assertions are about process
lifecycle, not values, and a hung child needs a supervisor the test framework
does not provide. It also must not link against the application.

So the smoke suite is a script under `scripts/quality/` with its own CI job,
invoked independently of `ctest`. This satisfies the requirement that the
default unit run stays fast without needing a tag mechanism.

### Decision 5 — load and soak tests live in the test binary under a tag

The opposite call, for the opposite reason: load tests exercise in-process
types (`AudioSampleFifo`, the analysis path) and want Catch2's fixtures and
reporting. They follow the existing `[.benchmark]` precedent with their own
tag, so `ctest` skips them by default and a dedicated invocation runs them.

Reusing the `[.benchmark]` tag itself was rejected: a benchmark answers "how
fast is one call" and is expected to be run and compared over time, whereas a
load test answers "does this survive pressure" and is pass/fail. Sharing a tag
would mean neither can be run without the other.

### Decision 6 — the manual checklist is a document plus dated run records

The checklist lives at `docs/development/manual-gui-checklist.md`; each run
produces a dated record naming the commit, platform, and audio device. Keeping
both in git makes "what do we verify" reviewable and "what did we verify before
v0.5.7" answerable, which is the whole point of writing it down.

Each item must state why automation cannot cover it. That is what keeps the
checklist shrinking as the smoke and unit suites grow, instead of becoming a
ritual nobody prunes.

### Decision 7 — the layout check is a Python script in CI

`scripts/` already has a Python test convention and `python-check.yml` already
runs on changes there, so a small script that walks `tests/` and `src/` and
fails on a `.cpp` at the `tests/` root — or a mirrored test path with no
corresponding `src/` directory — costs almost nothing and is itself testable
with the existing `scripts/run_tests.py` discovery.

The non-mirrored suites need an explicit allowlist in that script
(`tests/support/`, plus wherever the load suite lands), which doubles as the
documentation of what is deliberately outside the mirror.

## Risks / Trade-offs

- **[Risk] The concurrent FIFO tests are weak without TSan.** A data race that
  happens not to manifest on the runner will pass. → Mitigate by making the
  tests deterministic where possible (known sequences, exact-once assertions)
  and by explicitly noting in the test file that TSan (area 13) is the real
  verification. Do not let a green concurrent test be read as "the FIFO is
  proven correct".
- **[Risk] Smoke tests are the classic source of CI flakiness** — timing,
  window-manager races, and a virtual display that behaves subtly differently
  from a real one. → Bounded timeouts with clear failure messages, no reliance
  on pixel content, and a small number of high-value assertions rather than a
  broad UI script. If a smoke test flakes twice without a real defect, it should
  be deleted rather than retried.
- **[Risk] A published coverage number invites the wrong reaction** — writing
  tests to move the figure rather than to catch defects, particularly on the
  large untested `Component` files where a shallow instantiation test would move
  it a lot. → No threshold, and the report separates "not in the test build"
  from "covered 0%" so the honest backlog stays visible.
- **[Risk] Xvfb and gcovr are new CI-image dependencies** that can break a build
  independently of anything in this repository. → Both are widely-packaged and
  installed in the job rather than vendored; a failure is isolated to the
  coverage and smoke jobs, neither of which gates merge.
- **[Trade-off] A separate coverage build tree roughly doubles that job's build
  time** and cannot use ccache effectively. Accepted: one leg, non-gating, and
  the alternative is an unreliable figure.
- **[Trade-off] Soak tests are slow by construction**, so the version CI runs
  will be far shorter than a meaningful one. Accepted: the duration is
  configurable, CI runs a short one to prove the harness works, and a genuinely
  long run is a manual or scheduled activity.
- **[Risk] The manual checklist rots.** It is the item most likely to be written
  once and never run. → It states its cadence, its runs are committed records,
  and items must justify why they are not automated, so a stale item is visible
  as one nobody has recorded running.

## Migration Plan

Nothing to migrate; no runtime behaviour changes and nothing ships to users.
The build order keeps each step independently useful:

1. **Layout check** — smallest, and locks in the reorganisation this branch
   already landed before anything else builds on it.
2. **Coverage, all three languages, informational** — publish artifacts and a
   summary, then record the baseline in QA_STRATEGY. This is the priority item
   and it tells the remaining steps where to aim.
3. **Manual GUI checklist** — write it, then run it once and commit the first
   record so the format is proven rather than hypothetical.
4. **Smoke tests** — Xvfb job, then launch/tool/shutdown assertions.
5. **Load and soak tests** — concurrent FIFO first (highest value, area 13),
   then saturation, then the soak harness.

Rollback for any step is reverting its commits; each adds a CI job or a script
and none changes the application or the existing test suite.

## Open Questions

- **Where do manual checklist run records live?** A `docs/development/manual-runs/`
  directory of dated files is simplest and most reviewable; a single appended
  log is tidier but conflicts more. Leaning toward the directory.
- **Should coverage run on every PR or only on `main`?** Per-PR is more useful
  and is what Decision 1 assumes, but it adds a slow leg to every PR. If PR time
  becomes a problem, `main`-only plus a manual trigger is the fallback.
- **How many simultaneous tool consumers is "the maximum supported"?** The
  audio-thread contract documents one preallocated 65,536-sample FIFO per active
  tool consumer but does not state a cap. The load tests need a number; it may
  need to be decided and documented as part of this work.
- **Does the soak test need a real audio device?** If it must run in CI it
  cannot depend on one, which likely means driving `AudioInputService` through a
  synthetic source rather than a device — and that may itself require the kind
  of extraction area 9 describes.
