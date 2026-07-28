## Context

The `build-multiplatform.yml` workflow has a single `run_tests` boolean per
matrix leg that controls both `BUILD_TESTING` (passed to CMake) and an `if:`
guard on the `ctest` step. The test binary and test step already exist and are
correctly structured for every platform; only the flag value needs changing.

## Goals / Non-Goals

**Goals:**
- Run `PracticeTakesTests` on Linux arm64, Windows x64, and Windows arm64.
- Keep the change minimal: three flag flips, nothing else.

**Non-Goals:**
- macOS test execution — deferred (see decision 1).
- Adding new tests or changing test logic.
- Changing how `ctest` is invoked on any platform.

## Decisions

**1. macOS test legs remain `run_tests: false`.**
JUCE's `JUCEApplicationBase` and some audio components attempt to acquire
system audio resources or create UI message loops during static initialisation.
On a macOS CI runner there is no CoreAudio session, WindowServer, or display
connection, which can cause assertions or crashes before any test case runs.
Linux and Windows CI runners do not have this issue because JUCE's Linux/ALSA
and Windows/WASAPI backends are either headless-safe or not initialized by the
`PracticeTakesTests` harness. macOS will be addressed in a separate change
after verifying whether the test binary runs cleanly on a macOS runner with
`--no-gui` or equivalent JUCE bootstrap flags.

**2. All three non-macOS legs are enabled in a single change.**
Enabling them one at a time would produce three separate CI cycles with no
meaningful difference in risk. They share the same `ctest` invocation
structure; a failure on one is not caused by another.

**3. No changes to `BUILD_TESTING` defaults or CMake test configuration.**
`BUILD_TESTING` is already correctly wired; setting it to `true` activates the
`PracticeTakesTests` target and registers it with ctest. No CMake changes
are needed.

## Risks / Trade-offs

- **[Risk]** The arm64 Linux runner (`ubuntu-24.04-arm`) or the Windows arm64
  runner (`windows-11-arm`) may have different runner availability or
  reliability characteristics than x64. If flaky, `fail-fast: false` (already
  set) means a flaky arm64 run won't block the x64 package artifact. Accepted.
- **[Risk]** Windows-specific test failures may surface UB or MSVC-specific
  issues that require source fixes. This is the intended outcome — catching
  these before they ship is the point of the change.
