## Why

`PracticeTakesTests` currently runs only on Linux x64. The other five legs of
`build-multiplatform.yml` — Linux arm64, Windows x64, Windows arm64, macOS
Intel x64, macOS Apple Silicon — all build the binary but set `run_tests:
false`, so compiler bugs, UB, endianness issues, or platform-specific
regressions in DSP/audio code ship silently.

## What changes

**`.github/workflows/build-multiplatform.yml` — matrix flag changes only:**

| Leg               | Before | After  |
|-------------------|--------|--------|
| Linux x64         | `true` | `true` (unchanged) |
| Linux arm64       | `false`| `true` |
| Windows x64       | `false`| `true` |
| Windows arm64     | `false`| `true` |
| macOS Intel x64   | `false`| `false` (deferred) |
| macOS Apple Silicon | `false` | `false` (deferred) |

macOS is deferred because the `macos-15-intel` and `macos-15` runners have no
audio backend under CI (JUCE's CoreAudio requires a running session), which
may cause JUCE's internal assertions or test-harness setup to fail even when
all real test logic is correct. The Linux and Windows `ctest` invocations
already work correctly (the `ctest` step and its `--output-on-failure` flag
are already present in the workflow; only the `if:` guard needs enabling via
the matrix flag).

## New capabilities

- `cross-platform-test-execution`: `PracticeTakesTests` runs on Linux arm64,
  Windows x64, and Windows arm64 in addition to the existing Linux x64 leg.

## Impact

- Three additional `ctest` invocations per CI run.
- No source, build script, or test logic changes — only three `false` → `true`
  flag flips in the workflow matrix.
- macOS test execution is explicitly out of scope; a follow-up change can
  address it once JUCE's CI audio-backend requirements are understood.
