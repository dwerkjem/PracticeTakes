## Context

Catch2 v3.8.1 (the pinned version). The XML reporter (`--reporter XML`)
outputs `<BenchmarkResults>` elements with `mean`, `standardDeviation`, and
`outliers` child elements, all in nanoseconds. The JSON reporter does NOT
include benchmark data. The console reporter includes timing but is not
machine-parseable.

## Goals / Non-Goals

**Goals:**
- Post a readable markdown table to the GitHub step summary after every push
  to `main`.
- Measure HarmonicAnalyzer::analyze and AudioSampleFifo push/pop in addition
  to the existing PitchDetector benchmark.
- Measure app startup time (time-to-launch and first-frame visibility) as a
  separate probe, reported in the same summary.
- Upload raw XML for archival and future trend analysis.

**Non-Goals:**
- No PR-blocking on metric values (informational only).
- No historical trend database or regression thresholds in this change (those
  require weeks of baseline data — deferred).
- No cross-platform startup timing in this change (Linux only for now).
- No changes to existing test assertions or build flags.

## Decisions

**1. XML reporter, not console, not JSON.**
XML is the only Catch2 v3 reporter that emits `<BenchmarkResults>` with
numeric fields. JSON reporter confirmed to omit benchmark timing entirely
(returns only assertion counts). Console output is human-readable but not
reliably parseable.

**2. A dedicated Python formatter script rather than inline shell.**
The XML → markdown conversion has enough logic (ns → ms conversion, relative
stddev %, table alignment, startup JSON parsing) that inline shell would be
fragile and unreadable. A Python script is easier to test locally and extend.

**3. Startup timing via `time` + process exit code, not window appearance.**
GitHub Actions runners have no display server. Measuring "time to first window
pixel" requires a virtual framebuffer (Xvfb) and screenshot comparison, which
is complex. For now: measure the time from process spawn to clean exit when
the binary is invoked with a `--self-test` flag (or a short timeout kill),
which gives a reproducible proxy for startup initialisation cost. The script
produces a JSON sidecar with `launch_ms` and `exit_ms` fields.

**4. Informational workflow, separate from `build-multiplatform.yml`.**
Adding a `benchmarks.yml` workflow keeps concerns separate: the build workflow
focuses on artefact production, the benchmark workflow focuses on performance
data. The benchmark workflow does not call the reusable build workflow — it
builds the test binary directly using the same pattern as `cpp-quality-check.yml`.

**5. Weekly schedule + push-to-main triggers only.**
Running on every PR would generate per-PR noise from CI runner scheduling
variance. Weekly gives enough data for trend detection without spamming the
Actions tab. Push-to-main ensures every merge is recorded.

## Risks / Trade-offs

- **[Risk]** CI runner timing noise (shared vCPUs, CPU frequency scaling) will
  produce variance across runs. Accepted: the summary table shows stddev and
  relative stddev explicitly; the intent is to catch large regressions (>2×),
  not sub-percent variance.
- **[Risk]** The `--self-test` / timeout-kill startup probe is not identical to
  a real user launch (no audio device, no saved workspace state). Accepted:
  it measures the initialisation path consistently across commits, which is
  the goal.
