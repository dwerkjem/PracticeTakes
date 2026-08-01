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
- Golden-image or pixel comparison. The existing ui-golden harness stays exactly
  as it is; this change ended up borrowing nothing from it.
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

### Decision 3 — Xvfb for headless launch

> **Superseded in part.** The "reuse the X11 tools" half did not survive
> contact: the smoke driver reaches the application through the test control
> channel instead, so no X11 helper is involved, and geometry in the manual
> harness moved to the channel too when an external resize turned out to be
> refused below the window's advertised 980px minimum. Xvfb itself stands.

JUCE on Linux needs a real X server; there is no offscreen mode that exercises
the same window path. Xvfb is the standard answer, is a single apt package, and
is what the existing ui-validation tools already implicitly assume (they speak
Xlib).

The plan was to reuse `xwindow_capture` and `window_control` rather than growing
a second X11 helper set, and to reject a test-only control surface in the
application on the grounds that the X11 route needs nothing in `src/`.

**Both of those were overturned once the constraint was "no input synthesis".**
`pointer_control` turned out to do motion only — it cannot click — and the
application has no accelerators or menu bar, so there was nothing to drive with
either. A development-only control channel became the only route that does not
involve faking input, and it is gated so it never ships. See § "Resolved
Questions".

What survives is Xvfb, and the reasoning for it: JUCE on Linux needs a real X
server and there is no offscreen mode exercising the same path.

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

### Decision 6 — manual verification is a harness, not a document

A markdown checklist puts the whole burden on the tester: navigate to the right
place, remember what to look at, and transcribe the result afterwards. Each of
those is a way for a run to be skipped, done inconsistently, or not written
down — which is how manual checklists rot.

So manual verification is an interactive harness instead. It launches the
application, drives it to each surface, prompts in a terminal UI, and writes the
record itself. The tester's only job is to look and answer, which is the only
part a human is actually needed for.

The surface list is *data*, not code — a declarative definition of each surface,
how to reach it, and any extra questions — so adding a surface does not mean
editing the harness, and the definitions are reviewable on their own.

**Alternative considered:** the markdown checklist originally proposed. Rejected
because it optimises for being easy to write once and hard to run repeatedly,
which is exactly backwards for something whose value is entirely in being run
before every release.

### Decision 7 — three fixed axes plus per-surface extras

Every surface is scored on the same three questions: does it look correct, does
it look well-presented, does it work. Fixed axes are what make runs comparable —
you can see that presentation regressed on the tuner between two versions, which
a bespoke question set per surface cannot tell you.

Surfaces may then add their own questions, recorded separately so they do not
dilute the comparable core. A failure requires a note, because a recorded
failure with no detail costs more than it saves.

### Decision 8 — the scaling flag varies window geometry

An optional flag repeats each surface at several window sizes — a constrained
size, the default, and maximised. This targets the failure this application is
actually prone to: layouts that clip, overlap, or put controls out of reach when
the window is small. The existing `run-ui-golden.zsh` already exercises a
constrained 800x600 case, so the sizes have precedent.

Off by default, because it multiplies the number of prompts in a full run and
most runs do not need it.

**Alternative considered:** varying the desktop scale factor for HiDPI instead.
Deferred rather than rejected — it is a real concern, but it is a different
axis, and geometry is the one with existing evidence in the repository.

### Decision 9 — Textual for the TUI, which makes `adopt-uv-for-python` a
prerequisite

The harness is built with Python and Textual. Textual would be the repository's
**first third-party Python dependency**: there is no `requirements.txt`, no
`pyproject.toml`, and no lockfile anywhere today, and `python-check.yml` runs on
the runner's preinstalled interpreter with no install step.

This is safe to take on because Python is developer tooling that is never
shipped — `packaging/` contains no Python reference at all, and the released
artifact is the C++ binary. A dependency here reaches contributors, not users.

The unmerged `adopt-uv-for-python` change is the enabling piece, and its own
proposal says so directly: the scripts are stdlib-only "precisely because taking
a dependency currently means asking every contributor to `pip install` into
whatever environment they happen to have", and `uv add` is "the point at which a
script can reasonably start using a real library". It is already scoped to local
development only, which is exactly this harness's scope. **This change should
land after it**, or otherwise define its own dependency mechanism — which would
be inventing a second one for no reason.

One boundary must hold regardless: the three scripts `pre-commit` invokes
(`secrets_manager.py` twice, `run_clang_format.py`) and anything CI runs on the
preinstalled `python3` must stay stdlib-only. The dependency stays confined to
the harness.

### Decision 10 — the release gate matches records by code state, not commit id

Blocking a version bump on "a full manual run for the current commit" has a
chicken-and-egg problem that has to be solved explicitly or the gate is
unsatisfiable: the run verifies the app built from commit A, and the record of
that run is then committed as commit B. **A record can never name the commit
that contains it.** A naive `record.commit == HEAD` check would fail every time.

So the gate accepts a record when two things hold:

1. the verified commit is an ancestor of, or identical to, the commit being
   released — the run happened on this line of development, not a side branch;
   and
2. no *release-affecting* file differs between the verified commit and the
   release commit.

That second condition is the real check, and it is what makes committing the
record itself harmless: a commit that only adds a run record changes nothing
that affects the built application, so the record stays current.

"Release-affecting" is an explicit, documented path list — `src/`,
`CMakeLists.txt`, `cmake/`, `packaging/`, `vcpkg.json` — rather than an
inference. `tests/`, `docs/`, `scripts/`, `openspec/`, and `services/` do not
affect the desktop binary, so changing them does not invalidate a manual run.
Making the list explicit means a gap in it is a reviewable mistake rather than
silent under-enforcement, and adding a new release-affecting directory is a
deliberate act.

`VERSION` is deliberately excluded even though it is read by CMake, because the
dispatch path bumps it as part of releasing — including it would make the gate
unsatisfiable for exactly the case it exists to guard.

**Where it runs:** at the front of `release.yml`, before any artifact is built,
covering both entry points — the `workflow_dispatch` that bumps `VERSION` and a
pushed `v*` tag. Not on pull requests: an ordinary PR must not fail for lack of
a manual run.

**On waived failures.** A full run with a failed item blocks by default, but the
record may carry a written waiver per failed item. Without that, the only way to
ship with a known cosmetic defect is to bypass the gate entirely, and a gate
people routinely bypass stops being a gate. A waiver keeps the decision recorded
in the release's own history.

**On skipping.** The gate has an explicit skip flag, defaulting to off. Plenty of
releases — a docs fix, a CI tweak, a dependency pin — genuinely do not warrant a
full manual run, and a gate with no legitimate escape hatch gets bypassed in
ways that leave no trace at all.

The design constraint is that skipping must be *visible*, not that it must be
hard. So a skip requires a written reason, and the skip and its reason are
recorded with the release rather than only in workflow logs. That keeps "which
releases shipped without manual verification, and why" answerable months later,
which is the question that actually matters. On the `workflow_dispatch` path the
flag and reason are workflow inputs; the tag-push path has no inputs, so its
equivalent is a committed marker, and that asymmetry should be documented rather
than papered over.

**Alternative considered:** having the harness write the record and the version
bump in one commit, so the ids do match. Rejected — it welds a manual local tool
to the release process, and it still breaks the moment anything else lands
between the run and the release.

### Decision 11 — the layout check is a Python script in CI

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
- **[Risk] The harness itself becomes the thing that breaks.** A tool that
  drives the GUI depends on the GUI's structure, so a UI change can leave the
  harness unable to reach a surface — and a harness that fails to launch is a
  release check that silently does not happen. → Surfaces are declarative data,
  so a broken one is a one-line fix rather than a code change; an unreachable
  surface is recorded as a *failure* of that surface rather than skipped; and
  the harness is exercised by a quick-mode run often enough that breakage is
  found between releases rather than during one.
- **[Risk] Textual is the repository's first third-party Python dependency** and
  lands in a repo with no dependency file, no lockfile, and pre-commit hooks
  running on an ambient interpreter. → Land after `adopt-uv-for-python`, and
  keep the dependency confined to the harness: the three pre-commit scripts and
  anything CI runs must stay stdlib-only. Verified that `packaging/` references
  no Python, so nothing here reaches a shipped artifact.
- **[Risk] The skip flag becomes the default habit**, and the release gate
  quietly stops meaning anything. → It defaults to off, requires a written
  reason, and the skip is recorded with the release rather than only in workflow
  logs — so a run of consecutive skipped releases is visible rather than
  invisible. This is deliberately a visibility control, not a barrier: the
  maintainer is the only user, and a barrier they cannot pass would just get
  removed.
- **[Risk] Quick mode becomes the only mode anyone runs**, and a quick run gets
  mistaken for a release check. → The mode is recorded with the run and an
  incomplete run is marked as such, so a record cannot be read as more than it
  was. Full mode's value depends on it actually being run before releases, which
  is a discipline this change can support but not enforce.
- **[Risk] Manual verification rots regardless.** It is still the item most
  likely to be written once and never run. → Driving and recording are
  automated, so the marginal cost of a run is answering prompts; the cadence is
  stated; and questions must be deleted once automation covers them, so the run
  gets shorter over time rather than longer.

## Migration Plan

Nothing to migrate; no runtime behaviour changes and nothing ships to users.
The build order keeps each step independently useful:

1. **Layout check** — smallest, and locks in the reorganisation this branch
   already landed before anything else builds on it.
2. **Coverage, all three languages, informational** — publish artifacts and a
   summary, then record the baseline in QA_STRATEGY. This is the priority item
   and it tells the remaining steps where to aim.
3. **Manual GUI harness** — depends on `adopt-uv-for-python` landing first (see
   decision 9). Build the surface-definition format and the TUI, then run it
   once end to end and commit the first record so the format is proven rather
   than hypothetical. Quick mode first, since it is a strict subset of full and
   proves the driving mechanism with the fewest surfaces.
4. **Release gate** — after the harness, since it reads the records the harness
   produces. Land the gate and its skip flag together; a gate without an escape
   hatch gets bypassed rather than used.
5. **Smoke tests** — Xvfb job, then launch/tool/shutdown assertions.
6. **Load and soak tests** — concurrent FIFO first (highest value, area 13),
   then saturation, then the soak harness.

Rollback for any step is reverting its commits; each adds a CI job or a script
and none changes the application or the existing test suite.

## Resolved Questions

- **How does the harness drive the application to a surface?** Resolved
  2026-07-31: **no input synthesis of any kind.** Not keyboard, not mouse. The
  application instead exposes a development-only control channel with a *closed
  vocabulary* of two operations — put yourself into an approved named window
  state, and ask an approved named object to act as though clicked. The click
  invokes the object's own action in process; no pointer moves and no button
  event is faked.

  Three facts found while investigating, which are why coordinate-driving was
  never really on the table:

  - `pointer_control` does **not** click. It is `XTestFakeMotionEvent` only, and
    `run-ui-golden.zsh` uses it purely to *park* the pointer away from the UI so
    hover states do not pollute golden images. An earlier draft of this document
    said it could click; that was wrong.
  - The application has no keyboard accelerators beyond F11 and Escape, and no
    menu bar. Tools open through a `toolsButton` that raises a `PopupMenu`, so
    there is nothing to navigate to by key.
  - `openTool(ToolType, Presentation)` already exists as a clean entry point
    that maps one-to-one onto the notion of a surface.

  So coordinate clicking would have meant inventing click synthesis *and*
  binding the harness to pixel positions — paying for the fragility this design
  exists to avoid. Naming states and objects costs less and cannot silently
  redirect a click to the wrong control.

  `window_control` is retained for geometry only (`activate`, `maximize`,
  `restore`, `resize` by PID). Resizing a window is not input synthesis, and it
  is what decision 8's geometry sweep needs.

  The channel is gated behind a CMake option, following the
  `PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB` precedent, so it is absent from
  release builds.

- **Where do manual run records live, and in what format?** Resolved
  2026-08-01: `docs/development/manual-runs/`, one dated pair of files per run —
  JSON and rendered Markdown side by side. A directory rather than one appended
  log, because a log conflicts on every concurrent edit and is harder to diff.
  Both forms because they answer different questions: JSON lets two runs be
  compared question by question, Markdown is what a person reads. The mode is in
  the filename so a directory listing distinguishes a quick run from a release
  check without opening anything.

- **Which surfaces belong in quick mode?** Resolved 2026-08-01: the shell with
  no tool open, the tuner docked with live input, and the settings window. The
  smallest set that answers "does it still work" — the shell renders, a tool
  runs against real audio, and the most-used secondary window opens. A test
  asserts quick mode stays at five surfaces or fewer, because a quick mode that
  grows becomes a second full mode.

- **How does the harness take a Python dependency?** Resolved 2026-08-01: a root
  `pyproject.toml` declaring `textual` under an optional `manual-gui` extra,
  with `dependencies` deliberately empty. Forward-compatible with
  `adopt-uv-for-python`, which plans `uv add` against exactly this file, rather
  than a competing mechanism. The empty core is the enforcement: the scripts
  `pre-commit` invokes and everything CI runs on the preinstalled interpreter
  stay standard-library only, and the harness's tests skip its TUI cases when
  Textual is absent so the ordinary Python suite needs nothing installed.

- **Whether coverage runs per-PR or only on `main`.** Resolved 2026-08-01:
  **per-PR**, as decision 1 assumed. A figure nobody sees on their own pull
  request is a figure nobody acts on. Only one leg does the C++ build, and no
  job gates merge, so the cost is a slow leg on an informational workflow rather
  than a slower merge. If PR time becomes a problem, `main`-only plus a manual
  trigger is the fallback and needs no redesign.

- **The maximum supported number of simultaneous tool consumers.** Resolved
  2026-08-01: **not decided, and deliberately labelled as such.** The
  audio-thread contract states no cap, and inventing one here would put a number
  in the documentation that nothing enforces. The load tests use eight — the
  current tool count plus headroom — and say in the test file that it is
  provisional rather than agreed. A real cap belongs with whoever adds the tool
  that makes it matter.

- **Whether the soak test needs a real audio device.** Resolved 2026-08-01:
  **no.** It exercises `AudioSampleFifo` directly with a synthetic producer,
  which is where drift and unbounded growth would show, and needs no device or
  extraction work. That keeps it runnable in CI. Driving the whole capture path
  end to end under sustained load does still need either a device or the
  area-9 extraction, and that remains future work rather than something this
  change pretended to deliver.

## Open Questions

All resolved — see § "Resolved Questions". Kept as a heading so a future reader
does not assume the section was never filled in.


