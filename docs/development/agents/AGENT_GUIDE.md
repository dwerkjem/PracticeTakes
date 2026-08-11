# Agent guide

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository. The root `CLAUDE.md` is a stub that imports it, so
that the repository root stays small.

## Project

Practice Takes is an early-stage desktop music-practice application (JUCE 8,
C++20, CMake) for Windows/Linux/macOS, currently a tuner + spectrogram with a
shared microphone-capture shell. It is maintained by a single developer, so
process is intentionally light — see `.github/CONTRIBUTING.md` for when a change
needs an OpenSpec proposal (`openspec/changes/`) versus a straight PR.

There is also a small Cloudflare Worker service, `src/services/feedback-intake`
(TypeScript), that receives in-app feedback submissions.

## Commands

### Build and run (C++, Linux)

```bash
./tools/scripts/build/build-and-run.sh                    # configure, build, run
./tools/scripts/build/build-and-run.sh --build-only        # configure + build only
./tools/scripts/build/build-and-run.sh --clean
./tools/scripts/build/build-and-run.sh --jobs 2            # limit parallel compiles
BUILD_TYPE=Release ./tools/scripts/build/build-and-run.sh
./tools/scripts/build/build-and-run.sh --install-dependencies
```

Generic CMake (any platform):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target PracticeTakes --parallel
```

On a Linux machine with Nix installed alongside distro packages, force the
system toolchain (Nix's empty `CMAKE_SYSTEM_PREFIX_PATH` breaks
`find_package(X11)`):

```bash
/usr/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++
```

`build-and-run.sh` already does this for you. Use a Ninja tree
(`-G Ninja`) when you need `build/compile_commands.json` from an IDE-style
generator that wouldn't otherwise produce it.

### C++ tests (Catch2, `PracticeTakesTests`)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --target PracticeTakesTests --parallel
ctest --test-dir build --output-on-failure
```

Run a single test / tag directly through the binary (Catch2 CLI):

```bash
build/PracticeTakesTests "Test name or wildcard*"
build/PracticeTakesTests "[tag]"
build/PracticeTakesTests "[.benchmark]"          # opt-in perf benchmarks
```

`PracticeTakesTests` links into the build root, not `build/bin/` — only
`PracticeTakes` sets a `RUNTIME_OUTPUT_DIRECTORY`.

Performance work runs through the testing suite rather than inside the
application — `uv run test-suite run --kind performance` runs the `[.benchmark]`
cases and records every measurement against the machine it ran on. The
in-application Performance Lab was retired for that reason.

### C++ formatting and static analysis

```bash
pre-commit install                                   # once per clone
pre-commit run --all-files                            # or: pre-commit run clang-format --all-files
python tools/scripts/quality/run_clang_tidy.py $(find src -type f -name "*.cpp" -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
python tools/scripts/quality/run_clang_tidy.py --fix $(find src -type f -name "*.cpp" -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
python tools/scripts/quality/run_clang_format.py $(find src -type f \( -name "*.cpp" -o -name "*.h" \) -not -path "src/tests/*" -not -path "*/node_modules/*" | sort)
```

`clang-tidy` requires a built tree first (JUCE generates `JuceHeader.h` during
build). `CLANG_FORMAT`/`CLANG_TIDY`/`CLANG_TIDY_BUILD_DIR` env vars override
tool paths / build dir. Local pre-commit only runs `clang-format`; `clang-tidy`
runs in CI (PR check-only, plus an auto-fix commit to `main`).

### TypeScript service (`src/services/feedback-intake`)

```bash
cd src/services && npm ci
npm run check   # tsc --noEmit, fans out to every workspace
npm run test    # vitest run, fans out to every workspace
```

### Python scripts (`tools/scripts/`)

```bash
python tools/scripts/run_tests.py
```

Discovers every `test_*.py` under `tools/scripts/` by path (not
`unittest discover`, which silently finds nothing here — see the script's
docstring). An empty result is a hard error, not a silent pass.

### UI golden-image validation

```bash
./tools/scripts/quality/ui-validation/run-ui-golden.zsh
```

Captures first-launch and restored-workspace reference screenshots and times
fresh launches against them; writes evidence to `build/ui-validation/step-7/`.

### The testing suite

A standalone application under `tools/scripts/testing_suite/`, not part of
Practice Takes. `uv run test-suite` with no arguments opens a hub listing every
suite the project has — C++ tests, Python script tests, service tests, the smoke
test, benchmarks, golden images, UI capture — and builds whatever a selected
suite needs before running it.

```bash
uv run test-suite                        # the hub
uv run test-suite run --all              # same thing without a browser
uv run test-suite run --kind performance
uv run test-suite attend                 # only the questions an image cannot answer
uv run test-suite export                 # the record the release gate reads
uv run test-suite sync                   # share run history through git
```

Runs accumulate in a machine-local SQLite store — captures, verdicts, suite
results, measurements, all against one run — while the exported record under
`docs/development/quality/manual-runs/` stays the release gate's only input.
`sync` writes one JSON file per run to `docs/development/quality/run-history/`
so pass rates and performance trends travel through git; images never do. See
`docs/development/quality/TESTING_SUITE.md`. No CI check runs any of it.

## Architecture

Full detail lives in `docs/development/architecture/ARCHITECTURE.md` — read it before any
change that touches ownership, the audio thread, or adds a new tool/service.
Summary:

### Repository root (hard constraint)

The root is kept minimal on purpose. The only tracked top-level entries are
`src/`, `docs/`, `openspec/`, `tools/`, `README.md`, `CMakeLists.txt`,
`CLAUDE.md`, `LICENSE`, `pyproject.toml`, `uv.lock`, and dotfiles whose tooling
requires root placement. `pyproject.toml`/`uv.lock` are there because `uv`
resolves the project from the root; the package itself still lives under
`tools/scripts` via `[tool.setuptools] package-dir`.
`CLAUDE.md` is a stub that imports this guide — edit the guide, not the stub.

**Never add a new top-level file or directory.** Source of any language goes
under `src/` (`src/services` is the TypeScript worker, `src/tests` the C++
tests); build/packaging/tooling inputs under `tools/` (`tools/cmake`,
`tools/packaging`, `tools/scripts`, `tools/secret-patterns`, `tools/VERSION`,
`tools/vcpkg.json`); community-health files under `.github/`; documentation
and shared schemas under `docs/` (`docs/contracts`). `openspec/` is the one
exception that could not move: its CLI searches upward for an `openspec/`
directory, so it would not be found from a subdirectory. If something else
genuinely cannot work outside the root, say so explicitly rather than adding
it silently.

The root is the only place kept flat. Everywhere below it, prefer nesting: a
new document goes in the `docs/development/` subdirectory that matches its
subject (`build/`, `architecture/`, `quality/`, `performance/`,
`operations/`, `formats/`, `agents/`) rather than loose beside the index, and a new
subdirectory is the right answer when a subject grows. Add it to the index in
`docs/development/README.md`.

### Source layering (`src/`)

- `src/bootstrap` — JUCE application entry, owns the top-level `DocumentWindow`.
- `src/application/configuration` — settings persistence, defaults, portable
  `.ptsettings` import/export/transaction codecs.
- `src/application/theme` — shared palettes and look-and-feel.
- `src/application/shell` — window/menu/audio-state/workspace coordination.
  `shell/ui/{main_window,feedback,settings,workspace}` and `shell/state/{appearance,audio}`
  keep this from becoming one monolithic folder; `MainComponent` is split
  across files by responsibility rather than defined in one giant source file.
- `src/application/tools` — the contract every analysis tool is declared and
  built through: `ToolCatalog` (JUCE-free identity, aliases, instance policy),
  `ToolRegistry` (factories), `ToolComponent` (what the shell may assume of a
  tool), `ToolServices` (the shared-service bundle). `BuiltInToolCatalog.h` and
  `BuiltInTools.cpp` are the single registration site — adding a tool is one
  entry in each and no shell change. See
  `docs/development/architecture/adding-a-tool.md`.
- `src/application/testcontrol` — the stdin/stdout command channel the capture
  harness drives the running application through, opened only when
  `main.cpp` sees `--test-control`. Newline-delimited commands in, replies out,
  no socket and no port; the channel dies with the process. It applies an
  approved window state, clicks a named target, sets geometry/theme, and quits.
  See `docs/development/quality/TESTING_SUITE.md`.
- `src/features` — user-facing tools: `analysis/{tuner,spectrogram,harmonics}`,
  `feedback`, `performance` (launch timing).
- `src/platform` — shared infrastructure: `audio/` (`AudioInputService`,
  `AudioSampleFifo`, `AudioRecoveryPolicy`, `SyntheticTone`) and `score/` — a
  JUCE-free score model (`Score`, `TempoMap`, `Pitch`, `MusicalTime`) plus a
  MusicXML importer under `score/musicxml/`. `score/` has **no application
  consumer yet**: it is reached only from `src/tests/platform/score/`, so it is
  headless infrastructure landed ahead of the feature that will use it. What
  the importer accepts, drops with a diagnostic, or refuses is specified in
  `docs/development/formats/musicxml-subset.md`.

A `src/features/*` tool must never reach into another tool's internals —
shared behavior belongs in `src/platform` or `src/application`. A tool reaches
application state only through the `ToolServices` bundle it is constructed with,
never by reaching back into the shell.

### Ownership model

`PracticeTakesApplication` owns the main window, which owns one
`MainComponent` — the application shell. `MainComponent` owns the single
shared `AudioDeviceManager`, the app `LookAndFeel`, the Settings window, and one
`LiveTool` entry per tool instance — holding that instance's component, its
docked/floating presentation container, its presentation state, and its last
saved bounds and settings. `MainComponent` names no individual tool; it
discovers them through the registry. **Presentation containers (`DockedToolPanel`, `ToolWindow`, tab
components) never own the tool component** — moving a tool between docked,
floating, and tabbed presentation reparents it without destroying its
analysis state or shared-audio registration. Use `std::unique_ptr`/RAII for
single-owner objects; references for shared long-lived services.

`MainComponent::liveTools` is declared **after** the services tools borrow
(`audioInputService`, `appLookAndFeel`), so reverse-order member destruction
destroys every tool first. That ordering is the whole of how "a tool cannot
outlive its shared services" is enforced — there is no runtime check. Do not
move the declaration.

### Audio-thread boundary (hard constraint)

`AudioInputService::audioDeviceIOCallbackWithContext` runs on the real-time
audio thread and must stay bounded: no heap allocation, locks, blocking waits,
file/network I/O, logging, UI updates, or FFT/pitch-detection work. It only
clears output, reads atomic mute/gain, measures peak, and copies samples into
one preallocated 65,536-sample SPSC FIFO per active tool consumer. Each tool
drains its own FIFO from a message-thread timer and does its analysis there,
so a slow tool can't block capture or another tool. Device
start/stop/sample-rate changes are communicated via atomics polled on a timer,
not shared mutable state. See `docs/development/performance/audio-thread-safety.md`
for the fuller contract and `docs/development/architecture/ARCHITECTURE_QA.md` for the PR
checklist version of these rules.

The callback is annotated `noexcept PRACTICE_TAKES_NONBLOCKING`, and a
RealtimeSanitizer job fails any pull request that introduces an allocation, a
lock, or a blocking call inside it. That covers only the paths
`src/tests/platform/audio/AudioInputServiceTests.cpp` drives, and only under
Clang — the rules above still bind everywhere else. Keep the annotation and its
`noexcept` when touching the signature.

### Testability pattern

Pure logic (state machines, layout trees, policy decisions) is deliberately
split out of JUCE `Component` classes so it can be unit tested without a
display — e.g. `ui/workspace/model/WorkspaceLayoutState.h` has no JUCE
dependency and is covered by
`src/tests/application/shell/ui/workspace/model/WorkspaceLayoutStateTests.cpp`.
Follow this split for new non-trivial logic rather than embedding it directly
in a `Component` subclass. Roughly two fifths of `src/` (the JUCE
`Component`-heavy files: `FeedbackComponent.cpp`, `TunerComponent`,
`SpectrogramComponent`, `MainComponent*`) is currently outside
`PracticeTakesTests` for this reason. `AudioInputService.cpp` is now in the test
target, but only its audio callback is exercised — see
`docs/development/quality/QA_STRATEGY.md` area 9 before assuming a change there is
covered.

### Test layout

`src/tests/` mirrors `src/`, so a test's path names the source directory it
covers: `src/platform/score/TempoMap.h` is tested by
`src/tests/platform/score/TempoMapTests.cpp`. Put a new test beside the mirror
of the file it exercises rather than at the top level, and add its path to
`add_executable(PracticeTakesTests ...)` in `CMakeLists.txt`.

Both `src/` and `src/tests/` are on the test target's include path, so a test
includes its subject as `"platform/score/TempoMap.h"` and a shared fixture as
`"support/ScoreFixtures.h"`, whatever depth it sits at. `src/tests/support/`
holds fixtures shared across areas and is the one *code* directory that mirrors
nothing — it is the sole entry in `NON_MIRRORED_DIRECTORIES` in
`tools/scripts/quality/check_test_layout.py`, which enforces the mirror in CI.
The checker only inspects `.cpp`/`.h`, so test *data* lives outside the mirror
without an exemption: corpus files go under `src/tests/resources/`.

### Version

The application version lives only in `tools/VERSION` (CMake reads and
validates `MAJOR.MINOR.PATCH`, then propagates it into JUCE metadata and the
window title). `tools/scripts/release/version.py` is the only writer, and it
mirrors the value into `version-string` in `tools/vcpkg.json`. Never hardcode
or copy the version elsewhere.

### Feedback service contract

`docs/contracts/feedback/v1.schema.json` documents the wire format shared by the
C++ feedback client (`src/features/feedback`) and the
`src/services/feedback-intake` worker, but nothing currently validates either side
against it — they can drift (`docs/development/quality/QA_STRATEGY.md` area 12).

## Workflow notes

- Small, single-layer changes that follow an existing pattern go straight to
  a PR. Anything touching multiple layers, introducing a new shared
  service/ownership pattern, or changing the audio-thread contract goes
  through an OpenSpec proposal in `openspec/changes/` first — see
  `docs/development/architecture/ARCHITECTURE_QA.md § When to write more than a checklist
  pass`.
- The [architecture map](https://dwerkjem.github.io/PracticeTakes/) is a
  generated, browsable graph of the whole repo — useful for finding where a
  change belongs before starting.
- This repository configures Git merge drivers and `rerere`; `pre-commit
  install` sets them up. `CMakeLists.txt` source lists union automatically,
  and generated files under `.ua/` keep ours rather than merging. See
  `docs/development/operations/MERGING.md` before hand-resolving a conflict
  in either.
- Run the relevant test suite before requesting review: `PracticeTakesTests`
  for C++ changes under `src/**`/`src/tests/**`; `npm run check && npm run test`
  from `src/services/` for `src/services/**`; `python tools/scripts/run_tests.py` for
  `tools/scripts/**`.

## Agent skills

Configuration the installed engineering skills read. These files use
repository-specific paths rather than the skills' defaults, because the root
must stay minimal — see the hard constraint above.

### Issue tracker

Issues live as GitHub issues on `dwerkjem/PracticeTakes`, driven through the
`gh` CLI, and are labelled by `area:`/`type:`/`priority:`. GitHub is the
request surface; `openspec/changes/` remains the design surface. See
[`issue-tracker.md`](issue-tracker.md).

### Triage labels

The four triage states take a `status:` prefix to match this repository's label
families (`status:needs-triage`, `status:needs-info`,
`status:ready-for-agent`, `status:ready-for-human`); `wontfix` is reused
unprefixed. See [`triage-labels.md`](triage-labels.md).

### Domain docs

Single-context. The glossary is
`docs/development/architecture/CONTEXT.md` and ADRs go in
`docs/development/architecture/adr/` — **not** the skills' default root
`CONTEXT.md` and `docs/adr/`. See [`domain.md`](domain.md).
