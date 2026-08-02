# Agent guide

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository. The root `CLAUDE.md` is a stub that imports it, so
that the repository root stays small.

## Project

Practice Takes is an early-stage desktop music-practice application (JUCE 8,
C++20, CMake) for Windows/Linux/macOS, currently a tuner + spectrogram with a
shared microphone-capture shell. It is maintained by a single developer, so
process is intentionally light — see `CONTRIBUTING.md` for when a change needs
an OpenSpec proposal (`openspec/changes/`) versus a straight PR.

There is also a small Cloudflare Worker service, `services/feedback-intake`
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
build/bin/PracticeTakesTests "Test name or wildcard*"
build/bin/PracticeTakesTests "[tag]"
build/bin/PracticeTakesTests "[.benchmark]"          # opt-in perf benchmarks
```

Performance Lab (manual, opt-in instrumentation window) needs a separate
configure flag:

```bash
cmake -S . -B build -DPRACTICE_TAKES_ENABLE_PERFORMANCE_LAB=ON
./tools/scripts/quality/run-performance-lab.sh
```

### C++ formatting and static analysis

```bash
pre-commit install                                   # once per clone
pre-commit run --all-files                            # or: pre-commit run clang-format --all-files
python tools/scripts/quality/run_clang_tidy.py $(find src -type f -name "*.cpp" | sort)
python tools/scripts/quality/run_clang_tidy.py --fix $(find src -type f -name "*.cpp" | sort)
python tools/scripts/quality/run_clang_format.py $(find src -type f \( -name "*.cpp" -o -name "*.h" \) | sort)
```

`clang-tidy` requires a built tree first (JUCE generates `JuceHeader.h` during
build). `CLANG_FORMAT`/`CLANG_TIDY`/`CLANG_TIDY_BUILD_DIR` env vars override
tool paths / build dir. Local pre-commit only runs `clang-format`; `clang-tidy`
runs in CI (PR check-only, plus an auto-fix commit to `main`).

### TypeScript service (`services/feedback-intake`)

```bash
cd services && npm ci
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

## Architecture

Full detail lives in `docs/development/ARCHITECTURE.md` — read it before any
change that touches ownership, the audio thread, or adds a new tool/service.
Summary:

### Repository root (hard constraint)

The root is kept minimal on purpose. Tracked top-level entries are `src/`,
`tests/`, `docs/`, `services/`, `contracts/`, `openspec/`, `tools/`,
`README.md`, `CMakeLists.txt`, `CLAUDE.md`, `LICENSE`, and dotfiles whose
tooling requires root placement. `CLAUDE.md` is a stub that imports
`docs/development/AGENT_GUIDE.md` — edit the guide, not the stub.

**Never add a new top-level file or directory.** Build/packaging/tooling
inputs go under `tools/` (`tools/cmake`, `tools/packaging`, `tools/scripts`,
`tools/secret-patterns`, `tools/VERSION`, `tools/vcpkg.json`);
community-health files go under `.github/`; documentation goes under `docs/`.
If something genuinely cannot work outside the root, say so explicitly rather
than adding it silently.

### Source layering (`src/`)

- `src/bootstrap` — JUCE application entry, owns the top-level `DocumentWindow`.
- `src/application/configuration` — settings persistence, defaults, portable
  `.ptsettings` import/export/transaction codecs.
- `src/application/theme` — shared palettes and look-and-feel.
- `src/application/shell` — window/menu/audio-state/workspace coordination.
  `shell/ui/{main_window,feedback,settings,workspace}` and `shell/state/{appearance,audio}`
  keep this from becoming one monolithic folder; `MainComponent` is split
  across files by responsibility rather than defined in one giant source file.
- `src/features` — user-facing tools: `analysis/{tuner,spectrogram,harmonics}`,
  `feedback`, `performance` (Performance Lab / benchmarking).
- `src/services` — shared infrastructure, chiefly `audio/AudioInputService`
  and `AudioSampleFifo`.

A `src/features/*` tool must never reach into another tool's internals —
shared behavior belongs in `src/services` or `src/application`.

### Ownership model

`PracticeTakesApplication` owns the main window, which owns one
`MainComponent` — the application shell. `MainComponent` owns the single
shared `AudioDeviceManager`, the app `LookAndFeel`, the Settings window, one
live component per open tool, and each tool's docked/floating presentation
container. **Presentation containers (`DockedToolPanel`, `ToolWindow`, tab
components) never own the tool component** — moving a tool between docked,
floating, and tabbed presentation reparents it without destroying its
analysis state or shared-audio registration. Use `std::unique_ptr`/RAII for
single-owner objects; references for shared long-lived services.

### Audio-thread boundary (hard constraint)

`AudioInputService::audioDeviceIOCallbackWithContext` runs on the real-time
audio thread and must stay bounded: no heap allocation, locks, blocking waits,
file/network I/O, logging, UI updates, or FFT/pitch-detection work. It only
clears output, reads atomic mute/gain, measures peak, and copies samples into
one preallocated 65,536-sample SPSC FIFO per active tool consumer. Each tool
drains its own FIFO from a message-thread timer and does its analysis there,
so a slow tool can't block capture or another tool. Device
start/stop/sample-rate changes are communicated via atomics polled on a timer,
not shared mutable state. See `docs/development/performance-audio-thread-safety.md`
for the fuller contract and `docs/development/ARCHITECTURE_QA.md` for the PR
checklist version of these rules.

### Testability pattern

Pure logic (state machines, layout trees, policy decisions) is deliberately
split out of JUCE `Component` classes so it can be unit tested without a
display — e.g. `ui/workspace/model/WorkspaceLayoutState.h` has no JUCE
dependency and is covered by `tests/WorkspaceLayoutStateTests.cpp`. Follow
this split for new non-trivial logic rather than embedding it directly in a
`Component` subclass. Roughly a third of `src/` (the JUCE `Component`-heavy
files: `AudioInputService.cpp`, `FeedbackComponent.cpp`, `TunerComponent`,
`SpectrogramComponent`, `MainComponent*`) is currently outside
`PracticeTakesTests` for this reason — see
`docs/development/QA_STRATEGY.md` area 9 before assuming a change there is
covered.

### Version

The application version lives only in `tools/VERSION` (CMake reads and
validates `MAJOR.MINOR.PATCH`, then propagates it into JUCE metadata and the
window title). `tools/scripts/release/version.py` is the only writer, and it
mirrors the value into `version-string` in `tools/vcpkg.json`. Never hardcode
or copy the version elsewhere.

### Feedback service contract

`contracts/feedback/v1.schema.json` documents the wire format shared by the
C++ feedback client (`src/features/feedback`) and the
`services/feedback-intake` worker, but nothing currently validates either side
against it — they can drift (`docs/development/QA_STRATEGY.md` area 12).

## Workflow notes

- Small, single-layer changes that follow an existing pattern go straight to
  a PR. Anything touching multiple layers, introducing a new shared
  service/ownership pattern, or changing the audio-thread contract goes
  through an OpenSpec proposal in `openspec/changes/` first — see
  `docs/development/ARCHITECTURE_QA.md § When to write more than a checklist
  pass`.
- The [architecture map](https://dwerkjem.github.io/PracticeTakes/) is a
  generated, browsable graph of the whole repo — useful for finding where a
  change belongs before starting.
- Run the relevant test suite before requesting review: `PracticeTakesTests`
  for C++ changes under `src/**`/`tests/**`; `npm run check && npm run test`
  from `services/` for `services/**`; `python tools/scripts/run_tests.py` for
  `tools/scripts/**`.
