## ADDED Requirements

### Requirement: HarmonicAnalyzer benchmark in CI
`HarmonicAnalyzer::analyze` SHALL have a `[.benchmark]`-tagged Catch2
benchmark case that runs in CI and is included in the step summary report.

#### Scenario: Benchmarks run on push to main
- **WHEN** a commit is pushed to `main`
- **THEN** the `HarmonicAnalyzer::analyze` benchmark executes and its mean
  time appears in the GitHub Actions step summary

### Requirement: AudioSampleFifo benchmark in CI
`AudioSampleFifo::push` and `pop` SHALL have `[.benchmark]`-tagged Catch2
benchmark cases covering realistic audio block sizes (128, 256, 512 samples).

#### Scenario: Benchmarks run on push to main
- **WHEN** a commit is pushed to `main`
- **THEN** the AudioSampleFifo push and pop benchmarks execute and their
  mean times appear in the GitHub Actions step summary

### Requirement: Benchmark results posted as a readable step summary
All benchmark results SHALL be posted to the GitHub Actions step summary as
a markdown table showing benchmark name, mean time (ms), standard deviation
(ms), and relative standard deviation (%).

#### Scenario: Developer inspects a workflow run
- **WHEN** a developer opens the benchmark workflow run in GitHub Actions
- **THEN** the step summary shows a formatted markdown table with one row
  per benchmark, human-readable times in milliseconds, and a stddev % column

### Requirement: Startup timing metrics measured and reported
App time-to-launch and process-exit time SHALL be measured as startup
proxies and reported in the same step summary as the DSP benchmarks.

#### Scenario: Benchmarks run on push to main
- **WHEN** a commit is pushed to `main`
- **THEN** the step summary includes a startup metrics section with
  `launch_ms` (time to first output / process start overhead) and
  `exit_ms` (total process lifetime under a self-test invocation)

### Requirement: Raw benchmark XML uploaded as a build artifact
The raw Catch2 XML output SHALL be uploaded as a workflow artifact for
archival and future trend analysis.

#### Scenario: Developer wants to compare runs across commits
- **WHEN** a developer opens the benchmark workflow run
- **THEN** a downloadable artifact named `benchmark-results-<sha>.xml`
  is available for 90 days
