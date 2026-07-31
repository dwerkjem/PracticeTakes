## Why

Practice Takes has 288 passing unit tests and no idea what they cover. Nothing
measures coverage in any of the three languages — every figure in
`docs/development/QA_STRATEGY.md` had to be counted by hand, and the one number
that exists is alarming: `PracticeTakesTests` compiles 64 of 99 source files, so
roughly 9,000 of 16,100 lines never enter the test binary at all. The untested
third is not incidental code either; it is `AudioInputService`, the tuner and
spectrogram components, and the whole `MainComponent` shell — the parts a user
actually touches.

The suite is also uniformly one shape. Every test is a single-threaded,
in-process Catch2 unit test. Nothing launches the application, nothing exercises
the lock-free FIFO from two threads at once, nothing runs long enough to surface
a leak or a drift, and nothing records what a human checked by hand before a
release. A suite with one shape can only find one kind of bug.

Now is the moment because the groundwork just landed: `tests/` mirrors `src/`,
so for the first time a coverage report can be read against a test layout that
says which source directory each test belongs to.

## What Changes

- **Measure and publish coverage** for C++ (`gcov`/`llvm-cov`), TypeScript
  (`vitest --coverage`), and Python. Informational only — published as a CI
  artifact and a step summary, with **no threshold that can fail a build** until
  a baseline exists and has been looked at. This is what QA_STRATEGY area 8
  already prescribes.
- **Lock in the test layout** that just landed: `tests/` mirrors `src/`, and a
  check keeps it that way rather than trusting habit. `tests/support/` is the
  one exempt directory.
- **Add an interactive manual GUI verification harness** — not a document to
  read, but a tool that launches the application, drives it to each surface,
  prompts the tester in a terminal UI, and writes the run record itself. Every
  surface is scored on three fixed axes (does it look correct, does it look
  well-presented, does it work) plus any surface-specific questions. It has a
  **full** mode for pre-release verification and a **quick** mode for "does it
  still work", and an optional flag that repeats each surface at several window
  geometries. Today this knowledge exists only in the maintainer's head.
- **Add end-to-end smoke tests** that launch the real built application, assert
  it starts, opens a tool, and shuts down cleanly. Requires establishing a
  headless X story (Xvfb) — the repository has none today, and the existing
  `scripts/quality/ui-validation/` harness is X11-specific and local-only.
- **Add load and soak tests** that run the audio path under sustained and
  saturating conditions: FIFO producer/consumer contention, many concurrent tool
  consumers, and a long-running capture. These are opt-in (a Catch2 tag), not
  part of the default suite, because they are slow by construction.

Deliberately **not** in scope:

- Coverage thresholds or gating. Baseline first; gating is a later change once
  the number is known and trusted.
- Extracting testable logic out of the JUCE components (QA_STRATEGY area 9).
  This change measures the gap and makes it visible; closing it is separate work
  per component.
- Sanitizer builds (ASan/UBSan/TSan, area 13). The concurrent FIFO tests here
  will *want* TSan to be meaningful, and that dependency is called out, but
  adding sanitizer legs to CI is its own change.
- Enabling the macOS test legs (area 14), untested worker paths (area 10),
  roadmap tooling tests (area 11), and feedback contract conformance (area 12).
- Rewriting or restructuring any existing test's assertions. The layout moved;
  the tests themselves are not in scope.

## Capabilities

### New Capabilities

- `test-suite-layout`: the rule that `tests/` mirrors `src/`, what
  `tests/support/` is for, how a test resolves its subject and shared fixtures,
  and the check that keeps the tree from drifting back to flat.
- `test-coverage-reporting`: coverage measured for C++, TypeScript, and Python,
  published as an informational CI artifact and summary, explicitly
  non-gating, with a recorded baseline.
- `manual-gui-verification`: an interactive harness that drives the application
  to each surface, scores it on three fixed axes plus surface-specific
  questions, offers full and quick modes and an optional window-geometry sweep,
  and writes a dated run record itself.
- `end-to-end-smoke-tests`: headless launch of the built application, asserting
  startup, a tool opening, and clean shutdown, runnable in CI on Linux.
- `load-and-soak-tests`: opt-in sustained and saturating tests of the audio
  path, including genuinely concurrent exercise of `AudioSampleFifo`.

### Modified Capabilities

None. `cpp-pr-quality-gate` covers formatting and clang-tidy, `python-script-test-gate`
covers the Python suite running at all, and `benchmark-ci-reporting` covers
`[.benchmark]` timing reports — none of their requirements change. The load
tests sit beside the benchmarks rather than altering them: a benchmark measures
how fast one call is, a load test asserts the system survives sustained
pressure.

## Impact

- **`.github/workflows/`** — coverage steps added to the C++, TypeScript, and
  Python legs; a new headless smoke-test job needing Xvfb. `build-multiplatform.yml`
  already runs `ctest` on the Linux and Windows legs and is where C++ coverage
  attaches.
- **`CMakeLists.txt`** — a coverage build option (`--coverage`/`-fprofile-instr-generate`),
  kept off by default so ordinary builds are unaffected, plus any new test
  targets that must not be part of the default `PracticeTakesTests` run.
- **`tests/`** — new top-level areas that mirror nothing in `src/` because they
  are not unit tests of a source file: the smoke and load suites need a home and
  a naming rule, which the `test-suite-layout` capability must state.
- **`docs/development/QA_STRATEGY.md`** — areas 8 and 9's hand-counted figures
  get replaced by measured ones, and the manual checklist becomes a real
  document rather than a plan.
- **New tooling dependencies** — `gcovr` for the C++ report and Xvfb for
  headless launch, both CI-image packages that touch neither `vcpkg.json` nor
  `FetchContent`. Separately, **Textual** for the manual harness's TUI, which
  would be the repository's first third-party Python dependency: there is no
  `requirements.txt`, `pyproject.toml`, or lockfile today. This is acceptable
  because Python is developer tooling that is never shipped — `packaging/`
  contains no Python reference and the released artifact is the C++ binary — but
  it makes the unmerged `adopt-uv-for-python` change a **prerequisite**, since
  that change exists precisely to make taking a Python dependency reproducible.
  The three scripts `pre-commit` invokes must stay stdlib-only regardless.
- **Runtime cost** — a coverage build is slower and cannot reuse the normal
  build tree; load and soak tests are slow by design and must stay opt-in so the
  default `ctest` run stays fast enough to be run before every commit.
- **No behaviour change for users.** Nothing here ships in the application.
