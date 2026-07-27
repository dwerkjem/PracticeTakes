## 1. New benchmark test cases

- [ ] 1.1 Add `[.benchmark]`-tagged `BENCHMARK` cases to
      `tests/HarmonicAnalyzerTests.cpp`: one for `HarmonicAnalyzer::analyze`
      at 48 kHz and one at 44.1 kHz.
- [ ] 1.2 Add `[.benchmark]`-tagged `BENCHMARK` cases to
      `tests/AudioSampleFifoTests.cpp`: `push` and `pop` at 128, 256, and
      512 sample block sizes against a realistically-sized FIFO.

## 2. Benchmark formatter script

- [ ] 2.1 Create `scripts/quality/format_benchmarks.py` that:
      - Accepts the path to a Catch2 XML output file and an optional
        startup JSON sidecar as arguments.
      - Parses `<BenchmarkResults>` elements and converts ns → ms.
      - Writes a GitHub-flavoured markdown table to stdout with columns:
        Benchmark | Mean (ms) | Stddev (ms) | Stddev %.
      - If the startup JSON sidecar is present, appends a startup metrics
        section with `launch_ms` and `exit_ms`.

## 3. CI workflow

- [ ] 3.1 Create `.github/workflows/benchmarks.yml` triggered on `push` to
      `main` (paths: `src/**`, `tests/**`, `CMakeLists.txt`) and on a weekly
      schedule (`cron: '0 4 * * 1'`).
- [ ] 3.2 Workflow steps:
      a. Checkout, install build dependencies.
      b. Configure with `BUILD_TESTING=ON`, build `PracticeTakesTests` and
         `PracticeTakes`.
      c. Run `PracticeTakesTests "[.benchmark]" --reporter XML -o benchmark.xml`.
      d. Time a startup probe: launch `bin/PracticeTakes` under a 5-second
         `timeout`, capture wall time, write `startup.json`
         (`{"launch_ms": N, "exit_ms": N}`).
      e. Run `scripts/quality/format_benchmarks.py benchmark.xml startup.json`
         and append output to `$GITHUB_STEP_SUMMARY`.
      f. Upload `benchmark.xml` as artifact `benchmark-results-${{ github.sha }}`
         with 90-day retention.

## 4. Verification

- [ ] 4.1 Run the new benchmarks locally (`./build/PracticeTakesTests
      "[.benchmark]"`) and confirm all cases execute without error.
- [ ] 4.2 Run `format_benchmarks.py` against the XML output locally and
      confirm the markdown table renders correctly.
- [ ] 4.3 Confirm the workflow YAML is valid and the `$GITHUB_STEP_SUMMARY`
      append works as expected on a live push.
