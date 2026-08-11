## Why

A green test run currently means "no problem observed, on this scheduler, on
this machine, this once". `AudioSampleFifoLoadTests.cpp` says so at the top: it
runs a real producer and consumer on separate threads specifically so a race
detector has something to watch, and no race detector watches it.

There is no ASan, UBSan, or TSan build anywhere — not in CI, not in
`CMakeLists.txt`. Worse, the hardest constraint in the codebase, the
audio-thread contract in `docs/development/performance/audio-thread-safety.md`,
is enforced by prose and code review alone.

`AudioInputService.cpp` is not compiled into `PracticeTakesTests` at all, and no
test anywhere invokes `audioDeviceIOCallbackWithContext`. The rule that the
audio callback must not allocate, lock, or block has never been executed under
observation.

## What Changes

- **ASan + UBSan over the full suite, on every pull request.** One build tree,
  one job. Broad-spectrum: the FFT paths, the MusicXML importer, the score
  model, the settings codecs.
- **TSan over `[.load]`, nightly and on pushes to `main`.** Separate build tree,
  because ASan and TSan cannot coexist in one binary. Not on pull requests: the
  concurrency soak is the expensive leg and the PR budget is already eleven
  workflows deep.
- **A failing nightly opens an issue.** `area:testing`, `priority:p1`, naming
  the workflow, the run URL, and the failing step. Today a red scheduled run is
  visible only in the Actions tab, which is the same as invisible. Running on
  pushes to `main` as well makes a failure bisectable instead of leaving a week
  of commits to search.
- **A harness that actually runs the audio callback.** `AudioInputService`
  enters `PracticeTakesTests`, and a test constructs it, hands it buffers, and
  calls `audioDeviceIOCallbackWithContext`. This is a slice of #116 pulled
  forward deliberately, not scope creep: without it RealtimeSanitizer observes
  nothing and reports success.
- **RealtimeSanitizer on every pull request.** The callback is marked
  `[[clang::nonblocking]]` behind a portability macro, and RTSan aborts on any
  allocation, lock, or syscall inside it. This is the first executable
  enforcement the audio-thread contract has ever had.
- **A clang-20 spike before any of the RTSan work.** The project builds and
  tests with GCC 15.2 today; nobody has compiled it with clang. RTSan is
  clang-only.
- **A checked-in LeakSanitizer suppression file,** one comment per entry. JUCE,
  X11, ALSA, and the graphics drivers leak in ways this project cannot fix.

## Non-goals

- **No new tests beyond the callback harness.** The concurrency tests that TSan
  needs already exist. This change supplies observation, not coverage.
- **No retrofit of the two existing scheduled workflows.** `benchmarks.yml` and
  `secret-scan.yml` share the silent-failure blind spot; that is #159.
- **No change to the audio-thread contract itself.** The rules are unchanged.
  They become checkable rather than merely written.

## Capabilities

### New Capabilities

- `runtime-safety-verification`: what must be observed at runtime rather than
  asserted in prose — races, memory errors, undefined behaviour, and real-time
  safety — and the rule that a check nobody reads is not a check.

### Modified Capabilities

<!-- None. `cpp-pr-quality-gate` covers static analysis: clang-format and
     clang-tidy. Sanitizers observe a running program, which is a different
     capability rather than an extension of that one. -->

## Impact

- `.github/workflows/` — a sanitizer workflow with the PR legs, and a scheduled
  TSan workflow that opens an issue on failure.
- `CMakeLists.txt` — `AudioInputService.cpp` joins the test target; sanitizer
  options.
- `src/platform/audio/AudioInputService.h` — the `[[clang::nonblocking]]`
  attribute behind a macro, so the GCC build is unaffected.
- `src/tests/platform/audio/AudioInputServiceTests.cpp` — new; drives the
  callback.
- `tools/sanitizers/lsan.supp` — new.
- `docs/development/performance/audio-thread-safety.md` — records that the
  contract is now enforced, and how.
- **Coverage moves.** `AudioInputService.cpp` entering the test target changes
  the instrumented-TU count that #116 measures against, and the headline
  coverage figure with it.
- **If the clang spike fails,** this change still closes: ASan, UBSan, TSan, and
  the harness ship, and RTSan moves to a new issue blocked on a "build under
  clang" issue. A toolchain problem must not hold three working legs unmerged.
- **Not affected:** the Worker, the Python tooling, release packaging, and the
  runtime behaviour of the application itself.
