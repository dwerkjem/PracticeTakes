## Context

PracticeTakes currently has focused unit tests but no application-level facility for measuring realistic GUI and audio workflows on physical hardware. Performance experiments need a controlled way to run the same workload through different implementations, capture environmental provenance, and distinguish valid comparisons from results produced under materially different conditions.

The benchmark facility crosses the GUI, application orchestration, audio runtime, system telemetry, and persistence layers. Measurement must not introduce locks, allocation, file I/O, or GUI work into the real-time audio callback. Hardware results are authoritative; headless or virtualized automation can validate scenario logic and report schemas but cannot establish production performance.

## Goals / Non-Goals

**Goals:**

- Run repeatable end-to-end scenarios from an in-application GUI on real hardware.
- Compare a baseline with multiple selectable optimization strategies under equivalent conditions.
- Capture raw trials, statistical summaries, hardware/runtime provenance, warnings, and instrumentation status.
- Measure responsiveness, resource use, and real-time audio stability while preserving audio-thread safety.
- Make benchmark scenarios, metrics, strategies, and report formats independently testable.

**Non-Goals:**

- Automatically decide which optimization should ship.
- Treat CI virtual machines as substitutes for physical hardware measurements.
- Provide a general-purpose profiler or replace platform profiling tools.
- Guarantee reproducibility across materially different machines, builds, audio devices, or ambient system load.
- Enable optimization strategies in normal sessions unless separately promoted into production behavior.

## Decisions

### Use declarative scenarios with an application-level runner

Each scenario declares its workload identity, prerequisites, automation steps, completion criteria, applicable metrics, and comparison fields. An application-level runner owns warm-up, trials, cancellation, and state restoration while invoking existing commands and processing paths.

This is preferred over GUI-coordinate scripting because semantic actions are less fragile and can be tested without a display. It is preferred over isolated microbenchmarks because the purpose is to capture user-visible end-to-end costs. A small initial scenario set should cover launch/readiness, opening representative content, beginning playback or capture, and sustained analysis under a fixed workload.

### Select strategies through a stable registry

A strategy registry exposes a stable identifier, display name, implementation version, parameter schema, compatibility predicate, and factory/configuration hook. `baseline` always represents normal production behavior. Experimental strategies remain opt-in and benchmark-scoped.

Small optimizations use lightweight in-tree variants selected at runtime from the same commit and executable. A variant can wrap a local implementation choice or parameter set and is covered by registry, correctness, runner, and comparison tests, so creating a Git branch is not part of the testing contract. This avoids strategy-specific switches throughout the GUI and runner while keeping inexpensive experiments easy to measure. Compile-time-only or invasive strategies may still require separate builds or branches; their build, commit, and strategy identifiers become comparison fields rather than pretending they can be toggled at runtime.

### Split real-time event capture from non-real-time aggregation

The audio callback writes fixed-size timestamp and counter events to a preallocated lock-free channel. A non-real-time collector drains events and combines them with process CPU, memory, GUI latency, and scenario markers. Statistical summaries are computed only after a trial.

Direct logging or aggregation in the callback was rejected because it changes callback timing and can create the dropouts being measured. Instrumentation version and overhead status are stored with every run.

### Store immutable, versioned run records

Each run receives an identifier and stores configuration, provenance, raw trial data, summaries, warnings, status, and schema version. Persisted records are immutable; notes or labels may be separate metadata. JSON is the initial export format because it preserves structured raw measurements and is straightforward to consume from analysis tooling.

Comparability is computed from explicit fields rather than inferred from labels. The GUI may show incompatible runs side by side for inspection, but it must not calculate or label an improvement/regression without a clear warning.

### Present a work-focused performance lab

The GUI uses configuration, live-run, and results views. Configuration exposes scenario and strategy selectors, numeric trial controls, detected hardware/audio settings, and validation. Live-run shows phase, trial progress, cancellation, and safety warnings without high-frequency charts that add measurement noise. Results use a metric table and strategy comparison view with raw-trial drill-down and export.

The runner suppresses unrelated background work where the application can do so safely and records ambient-load checks before and during a run. A stabilization period precedes warm-up, but the system does not silently discard unfavorable trials.

## Risks / Trade-offs

- **Instrumentation changes measured performance** -> Keep audio-thread capture constant-time and allocation-free, version instrumentation, and run calibration scenarios to estimate overhead.
- **Thermal throttling and background load produce noisy results** -> Record run order, temperatures/frequencies when available, ambient CPU, stabilization time, and variability; support interleaved baseline/strategy trial order.
- **Strategies accidentally change behavior or output quality** -> Require scenario correctness assertions and output-equivalence checks before accepting performance comparisons.
- **Hardware metadata is platform-specific or unavailable** -> Isolate metadata providers by platform and preserve explicit unknown values rather than guessing.
- **GUI automation becomes brittle** -> Drive semantic application commands and observable state, limiting coordinate automation to external smoke validation.
- **Stored schemas evolve** -> Version records and implement read migrations while retaining original exported data.

## Migration Plan

1. Add result/schema types, strategy registry, and deterministic fake collectors behind a disabled development feature flag.
2. Add the runner with one representative scenario and validate cancellation, cleanup, and correctness without production telemetry.
3. Add audio-safe event capture and platform telemetry providers, then calibrate instrumentation overhead.
4. Add the GUI workflow, persisted run history, compatibility checks, and JSON export.
5. Establish a documented reference-hardware protocol and collect baseline runs before enabling additional strategies.
6. Keep the feature development-only until audio-thread safety, report compatibility, and scenario repeatability pass review; rollback is removal or disabling of the feature flag with no user-data migration required.

## Open Questions

- Which physical machines, operating systems, and audio interfaces form the initial reference-hardware matrix?
- Which representative project/audio fixture can be committed or generated without licensing or privacy concerns?
- Which platform APIs can reliably expose CPU frequency, temperature, and process energy without elevated privileges?
- Should compile-time strategies be automated as separate build artifacts in the first release or deferred until runtime strategies are proven?
- What minimum trial count and regression thresholds should become the default for each scenario after pilot measurements establish variance?