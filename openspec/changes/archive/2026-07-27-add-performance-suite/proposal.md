## Why

Real-time audio code has hard latency and allocation constraints (documented in
`ARCHITECTURE.md` and `CODE_STYLE.md`). Nothing currently measures whether a
change regresses the analysis pipelines or slows startup. One Catch2 benchmark
exists (`PitchDetectorTests.cpp`, `[.benchmark]`) but it never runs in CI and
there is no coverage for the harmonic analyzer, audio FIFO, or app startup time.

## What changes

**New benchmark test cases** added to existing test files:
- `tests/HarmonicAnalyzerTests.cpp`: `HarmonicAnalyzer::analyze` per-frame
  throughput at 48 kHz and 44.1 kHz.
- `tests/AudioSampleFifoTests.cpp`: `AudioSampleFifo::push` and `pop` at
  realistic audio block sizes (128, 256, 512 samples).

**New CI workflow** — `.github/workflows/benchmarks.yml`:
- Triggered on `push` to `main` and on a weekly schedule (not on every PR —
  CI runner noise makes per-PR absolute comparisons unreliable).
- Runs `PracticeTakesTests "[.benchmark]" --reporter XML` and parses the XML
  output into a GitHub step summary markdown table showing benchmark name,
  mean (ms), stddev (ms), and relative stddev (%).
- Measures **time to launch** and **first-paint visibility time** by timing
  `build/bin/PracticeTakes` startup under `timeout` with a `--headless-test`
  flag (process-exit time as a proxy on Linux CI where no display is
  available), parsed and posted to the same step summary.
- Uploads the raw XML as a build artifact (`benchmark-results-<sha>.xml`).
- Results are **informational only** — the workflow never fails on a metric
  value, only on a crash or test binary failure.

**New helper script** — `scripts/quality/format_benchmarks.py`:
- Reads the Catch2 XML output and produces a GitHub-flavoured markdown table
  for `$GITHUB_STEP_SUMMARY`.
- Also parses a startup-timing JSON sidecar (produced by the workflow's
  startup step) and appends a startup metrics section.

## New capabilities

- `benchmark-ci-reporting`: Benchmark results are published to the GitHub
  Actions step summary on every merge to `main`, in a human-readable table.
- `startup-timing`: App time-to-launch and first-paint visibility time are
  measured and reported alongside DSP benchmarks.

## Impact

- No changes to existing test logic, source, or build configuration.
- New workflow runs ~2 minutes on `ubuntu-24.04` (one benchmark invocation).
- Artifact retention: 90 days (enough for a few months of trend data).
- macOS and Windows startup timing deferred — the startup probe script targets
  Linux; cross-platform startup timing can follow in a later change.
