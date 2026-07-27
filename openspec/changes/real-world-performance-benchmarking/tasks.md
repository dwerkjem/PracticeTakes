## 1. Benchmark Contracts

- [x] 1.1 Define versioned types for run configuration, hardware/runtime provenance, raw trial measurements, summaries, warnings, and completion status
- [x] 1.2 Define scenario and optimization-strategy interfaces with stable identifiers, parameter schemas, compatibility checks, and correctness assertions
- [x] 1.3 Implement deterministic fake scenarios, strategies, clocks, and telemetry collectors for runner tests
- [x] 1.4 Add serialization round-trip and forward-version rejection tests for benchmark records.
- [x] 1.5 Add a lightweight in-tree strategy adapter and tests proving a parameterized optimization can run against baseline from the same commit and executable

## 2. Scenario Runner

- [x] 2.1 Implement the application-level runner state machine for stabilization, warm-up, measured trials, aggregation, and cleanup
- [x] 2.2 Add safe cancellation and failure handling that restores application state and preserves completed trials as incomplete
- [ ] 2.3 Implement one representative sustained audio-analysis scenario using semantic application actions and a deterministic fixture
- [ ] 2.4 Add runner tests covering successful runs, invalid configurations, correctness failures, cancellation, and cleanup

## 3. Metrics And Provenance

- [ ] 3.1 Implement preallocated real-time event capture for callback timing, deadline misses, dropouts, and underruns without callback logging or allocation
- [ ] 3.2 Implement non-real-time collection for process CPU, process memory, GUI latency markers, ambient load, and scenario throughput
- [ ] 3.3 Implement platform providers for OS, CPU, memory, audio device/backend, sample rate, buffer size, build, commit, and instrumentation metadata with explicit unknown values
- [ ] 3.4 Implement trial aggregation for count, median, tail percentile, minimum, maximum, variability, and event totals with deterministic tests
- [ ] 3.5 Add stress tests and an audio-thread safety review proving the event path is bounded, lock-free, and allocation-free

## 4. Strategy Comparison And Storage

- [ ] 4.1 Implement the strategy registry with the production baseline and at least one behavior-equivalent, runtime-selectable optimization fixture that requires no separate branch or build
- [ ] 4.2 Implement explicit comparability-field validation and tests for every material mismatch
- [ ] 4.3 Implement immutable local run persistence and loading with schema migration boundaries
- [ ] 4.4 Implement versioned JSON export containing raw trials, summaries, configuration, provenance, status, warnings, and instrumentation overhead status
- [ ] 4.5 Add comparison calculations and tests for absolute values, relative changes, missing metrics, zero baselines, and incompatible runs

## 5. Performance Lab GUI

- [ ] 5.1 Add a development-gated Performance Lab entry point and configuration view with scenario, strategy, trial, and audio controls plus inline validation
- [ ] 5.2 Add a stable live-run view showing phase, trial progress, safety warnings, and safe cancellation without high-frequency visualization
- [ ] 5.3 Add run-history and results views with provenance, statistical metric tables, raw-trial drill-down, and incomplete-run status
- [ ] 5.4 Add side-by-side strategy comparison that blocks improvement/regression labels for incompatible runs and explains mismatched fields
- [ ] 5.5 Add report export and saved-result reopening flows with GUI tests for configuration, progress, cancellation, comparison, and errors
- [ ] 5.6 Add a GUI test that selects, runs, and compares a lightweight parameterized strategy against baseline in one application session

## 6. Hardware Pilot And Acceptance

- [ ] 6.1 Document the initial reference-hardware matrix, ambient-load controls, stabilization procedure, trial ordering, and representative workload
- [ ] 6.2 Run instrumentation-off and instrumentation-on calibration scenarios to quantify or document overhead on each reference system
- [ ] 6.3 Execute interleaved baseline and experimental-strategy pilot runs, inspect variance and thermal behavior, and set scenario-specific default trial counts
- [ ] 6.4 Verify output equivalence and scenario correctness before accepting each strategy comparison
- [ ] 6.5 Complete an end-to-end hardware acceptance run covering configuration, execution, cancellation, persistence, reopening, comparison, and export
- [ ] 6.6 Keep the feature development-gated until repeatability, audio-thread safety, and report compatibility acceptance criteria are recorded and approved